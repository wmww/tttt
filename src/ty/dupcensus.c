// The cross-checker duplication census (TTTT_DUP_CENSUS=1): after checking, every checker's types, intern table
// entries and relation cache entries by structural fingerprint (fingerprint.inc: ids replaced by the referents'
// fingerprints, symbols and AST nodes by address, the same in every checker of one process), and how many checkers
// hold each one. A checker recycled meanwhile counts with its share. Prints, per class, the copies made against the
// distinct ones: how much of K checkers' work is the same work done again, and whether every checker (the lib and the
// project's hubs) or a few (which files a checker got) make it
#include "ty/ty_internal.h"
#include "error.h"
#include "scope.h"
#include "binding.h"
#include "source_file.h"
#include "function_impl.h"
#include "span.h"
#include <stdio.h>
#include <pthread.h>

#include "ty/fingerprint.inc"

#define INTERN_LONG_KEY 0xffffu // (checker.c's)

enum { DOMAIN_TYPE = 1, DOMAIN_TYPES_KEY, DOMAIN_LINKS_KEY, DOMAIN_NODE_LINK, DOMAIN_RELATION };
enum { CAT_MAX = 1024 };

static U64Map census; // (domain, fingerprint) -> checker mask | category << 32
static u32 census_checkers; // The highest share + 1
static u64 per_checker_types[64], per_checker_memory[64];
static u64 unhashed[8];

static char const* const kind_names[] = { "intrinsic", "literal", "type-param", "declared", "reference", "anonymous",
    "union", "intersection", "keyof", "indexed", "conditional", "substitution", "template", "string-mapping",
    "evolving-array" };
static char const* const home_names[] = { "-", "lib", "dts", "ts" };

static u32 symbol_home(Symbol const* symbol) {
    if (!symbol) return 0;
    SourceFile const* file = span_source(symbol->name_span);
    if (!file && symbol->decl_count) return 0;
    if (!file) return 0;
    return file->is_stdlib ? 1 : file->is_declaration ? 2 : 3;
}

static u32 function_home(FunctionImpl const* function) {
    SourceFile const* file = function ? function->span_source : nullptr;
    if (!file) return 0;
    return file->is_stdlib ? 1 : file->is_declaration ? 2 : 3;
}

// Where a type comes from: the alias, or the declaration it instantiates
static u32 ty_home(Ty const* ty, u32 depth) {
    if (!ty || depth > 8) return 0;
    if (ty->alias) return symbol_home(ty->alias);
    switch ((TyKind)ty->kind) {
        case TY_TYPE_PARAM: return symbol_home(ty->param.symbol);
        case TY_DECLARED: return symbol_home(ty->declared.symbol);
        case TY_REFERENCE: return ty_home(ty->reference.target, depth + 1);
        case TY_ANONYMOUS:
            if (ty->anonymous.function) return function_home(ty->anonymous.function);
            if (ty->anonymous.target) return ty_home(ty->anonymous.target, depth + 1);
            if (!ty->anonymous.is_reverse && ty->anonymous.merged_value) return symbol_home(ty->anonymous.merged_value);
            return 0;
        case TY_CONDITIONAL:
            if (ty->conditional.root && ty->conditional.root != ty) return ty_home(ty->conditional.root, depth + 1);
            return ty_home(ty->conditional.check, depth + 1);
        case TY_INDEXED: return ty_home(ty->indexed.object, depth + 1);
        case TY_KEYOF: return ty_home(ty->keyof.operand, depth + 1);
        case TY_SUBSTITUTION: return ty_home(ty->substitution.base, depth + 1);
        default: return 0;
    }
}

// TTTT_DUP_DUMP=PATH (with --checkers 1 --checker-memory 0: a fresh checker per file): what each file's check made,
// a quarter of the keys sampled by hash, written per file as (u32 file index, u32 key count, u64 source bytes, keys)
// for scripts that evaluate assignments of files to checkers offline (the union of a checker's files' sets)
static FILE* dump_file;
static bool dump_checked;
static u64* dump_keys;
static u32 dump_len, dump_cap;
static u32 dump_at[64]; // Per share: the file it checks (with a fresh checker per file, what its checker made)
static u64 dump_bytes[64];

void dup_census_file(u32 share, u32 at, u64 bytes) {
    if (share >= 64) return;
    dump_at[share] = at;
    dump_bytes[share] = bytes;
}

static void census_put(u32 domain, u64 fp, u32 category, u32 share) {
    if (!fp) {
        unhashed[domain]++;
        return;
    }
    u64 key = fp_mix(fp, domain) | 1;
    if (dump_file) {
        if ((mix64(key) & 3) != 0) return;
        if (dump_len == dump_cap) {
            dump_cap = dump_cap ? dump_cap * 2 : 1 << 16;
            dump_keys = realloc(dump_keys, sizeof(u64) * dump_cap);
            panic_if(!dump_keys, "out of memory");
        }
        dump_keys[dump_len++] = (key & ~((u64)0xff << 56)) | (u64)domain << 56;
        return;
    }
    u64* slot = map_find(&census, key, true, true, nullptr);
    *slot |= (u64)1 << share | (u64)(category & 0xffffff) << 32 | (u64)domain << 56;
}

static pthread_mutex_t census_lock = PTHREAD_MUTEX_INITIALIZER;

static void census_add_locked(Checker* me, u32 share);
#ifdef TTTT_ROOT_CENSUS
static void root_census_add(Checker* me, u32 share);
static void root_census_report(u32 k);
#endif

void dup_census_add(Checker* me, u32 share) {
    if (share >= 32) return;
    pthread_mutex_lock(&census_lock);
    census_add_locked(me, share);
    pthread_mutex_unlock(&census_lock);
}

static void census_add_locked(Checker* me, u32 share) {
    if (!dump_checked) {
        dump_checked = true;
        char const* path = getenv("TTTT_DUP_DUMP");
        if (path) {
            dump_file = fopen(path, "wb");
            panic_if(!dump_file, "can't write ", path);
        }
    }
    if (share + 1 > census_checkers) census_checkers = share + 1;
    Checker* was = current_checker;
    checker_make_current(me);
    per_checker_types[share] += checker_type_count(me);

    per_checker_memory[share] += checker_memory(me);
    map_clear(&ty_fps);
#ifdef TTTT_ROOT_CENSUS
    if (!dump_file) root_census_add(me, share);
#endif
    // Types: by kind and home
    for (u32 o = 0; o < my lazy.own_len; o++) {
        u32 index = my lazy.own[o];
        Ty const* const* chunk = my lazy.types[index];
        if (!chunk) continue;
        // (Malloc'd: only the slots of ids it gave out are written. Block 0 starts at id 1)
        u32 end = index == my next_type_id >> LAZY_TYPE_CHUNK_BITS ? (my next_type_id & ((1u << LAZY_TYPE_CHUNK_BITS) - 1)) : (1u << LAZY_TYPE_CHUNK_BITS);
        for (u32 i = index ? 0 : 1; i < end; i++) {
            Ty const* ty = chunk[i];
            if (!ty) continue;
            census_put(DOMAIN_TYPE, fp_ty(ty), (ty->kind < 15 ? ty->kind : 15) * 4 + ty_home(ty, 0), share);
        }
    }
    // Intern tables: by key kind; the kinds fp_key() knows by their key, the others by their raw words (addresses and
    // this checker's ids: an id key never matches another checker's, so those count as unique, a lower bound)
    InternTable* tables[2] = { &my types, &my links };
    for (u32 t = 0; t < 2; t++) {
        InternTable* table = tables[t];
        for (u32 i = 0; i < table->cap; i++) {
            if (!table->slots[i]) continue;
            void const** block = intern_slot_block(table, table->slots[i]);
            u32 count = intern_block_count(block);
            if (!count) continue;
            // (Word 0 as the key's maker gave it, without the count the block keeps above it)
            u64 words_buf[64];
            u64 const* stored = intern_block_words(block, count);
            u64 const* words = stored;
            if (count <= 64) {
                memcpy(words_buf, stored, sizeof(u64) * count);
                words_buf[0] &= INTERN_WORD0_MASK;
                words = words_buf;
            }
            u32 kind = (u32)(words[0] & 255);
            u64 fp = fp_key(me, kind, words, count);
            if (!fp) {
                fp = fp_mix(97, kind);
                bool ids = false;
                for (u32 w = 1; w < count; w++) {
                    // (A word that reads as one of this checker's type ids: its type's fingerprint)
                    Ty const* ty = words[w] > 1 ? key_type(me, words[w]) : nullptr;
                    if (ty) ids = true;
                    fp = fp_mix(fp, ty ? fp_ty(ty) : words[w]);
                }
                (void)ids;
            }
            census_put(t ? DOMAIN_LINKS_KEY : DOMAIN_TYPES_KEY, fp, kind, share);
        }
    }
    // Node links: (kind, address)
    for (u32 i = 0; i < my node_links.cap; i++) {
        LinkEntry const* entry = &my node_links.entries[i];
        if (!entry->key) continue;
        census_put(DOMAIN_NODE_LINK, fp_mix(101, entry->key), (u32)(entry->key >> 56), share);
    }
    // Relation caches: (relation, source, target)
    for (u32 r = 0; r < TY_RELATION_COUNT; r++) {
        RelationCache const* cache = &my relations[r];
        for (u32 i = 0; i < cache->cap; i++) {
            u64 key = cache->keys[i];
            if (!key) continue;
            Ty const* source = key_type(me, key >> 32);
            Ty const* target = key_type(me, key & 0xffffffffu);
            if (!source || !target) {
                unhashed[DOMAIN_RELATION]++;
                continue;
            }
            census_put(DOMAIN_RELATION, fp_mix(fp_mix(fp_mix(103, r), fp_ty(source)), fp_ty(target)), r, share);
        }
    }
    map_clear(&ty_fps);
    checker_make_current(was);
    if (dump_file) {
        u32 header[2] = { dump_at[share], dump_len };
        fwrite(header, sizeof(header), 1, dump_file);
        fwrite(&dump_bytes[share], sizeof(u64), 1, dump_file);
        fwrite(dump_keys, sizeof(u64), dump_len, dump_file);
        dump_len = 0;
    }
}

typedef struct CatStats {
    u64 copies, distinct, in_all, in_all_copies, in_one, in_few_copies, in_few;
} CatStats;

static void print_domain(u32 domain, char const* title, u32 k) {
    static CatStats cats[CAT_MAX];
    memset(cats, 0, sizeof(cats));
    u64 hist[33] = {0};
    for (u32 i = 0; i < census.cap; i++) {
        u64 key = census.keys[i];
        if (!key) continue;
        u64 value = census.values[i];
        if ((value >> 56) != domain) continue;
        u32 n = (u32)__builtin_popcount((u32)value);
        u32 cat = (u32)(value >> 32) & 0xffffff;
        if (cat >= CAT_MAX) cat = CAT_MAX - 1;
        CatStats* s = &cats[cat];
        s->copies += n;
        s->distinct++;
        if (n == k) s->in_all++, s->in_all_copies += n;
        else if (n == 1) s->in_one++;
        else s->in_few++, s->in_few_copies += n;
        hist[n]++;
    }
    CatStats total = {0};
    for (u32 c = 0; c < CAT_MAX; c++) {
        total.copies += cats[c].copies, total.distinct += cats[c].distinct;
        total.in_all += cats[c].in_all, total.in_all_copies += cats[c].in_all_copies;
        total.in_one += cats[c].in_one, total.in_few += cats[c].in_few, total.in_few_copies += cats[c].in_few_copies;
    }
    fprintf(stderr, "== %s: %llu copies, %llu distinct (x%.3f); extra copies %llu: in all %u checkers %llu, in 2..%u %llu; unhashed %llu\n",
        title, (unsigned long long)total.copies, (unsigned long long)total.distinct,
        total.distinct ? (double)total.copies / (double)total.distinct : 0.0,
        (unsigned long long)(total.copies - total.distinct), k,
        (unsigned long long)(total.in_all_copies - total.in_all), k - 1,
        (unsigned long long)(total.in_few_copies - total.in_few), (unsigned long long)unhashed[domain]);
    fprintf(stderr, "   holders histogram:");
    for (u32 n = 1; n <= k; n++) fprintf(stderr, " %u:%llu", n, (unsigned long long)hist[n]);
    fprintf(stderr, "\n");
    // Categories by extra copies
    for (u32 round = 0; round < 40; round++) {
        u32 best = CAT_MAX;
        u64 most = 0;
        for (u32 c = 0; c < CAT_MAX; c++) {
            u64 extra = cats[c].copies - cats[c].distinct;
            if (cats[c].copies && extra >= most && (best == CAT_MAX || extra > most)) most = extra, best = c;
        }
        if (best == CAT_MAX || !cats[best].copies) break;
        CatStats* s = &cats[best];
        char name[64];
        if (domain == DOMAIN_TYPE) snprintf(name, sizeof(name), "%s/%s", kind_names[best / 4 < 15 ? best / 4 : 14], home_names[best % 4]);
        else if (domain == DOMAIN_RELATION) snprintf(name, sizeof(name), "relation %u", best);
        else snprintf(name, sizeof(name), "key %u", best);
        fprintf(stderr, "   %-26s copies %9llu distinct %9llu extra %9llu (all-%u %9llu, few %9llu) single %9llu\n", name,
            (unsigned long long)s->copies, (unsigned long long)s->distinct, (unsigned long long)(s->copies - s->distinct), k,
            (unsigned long long)(s->in_all_copies - s->in_all), (unsigned long long)(s->in_few_copies - s->in_few),
            (unsigned long long)s->in_one);
        s->copies = 0;
    }
}

void dup_census_report(void) {
    if (dump_file) {
        fclose(dump_file);
        dump_file = nullptr;
        return;
    }
    u32 k = census_checkers;
    fprintf(stderr, "== dup census: %u checkers\n", k);
    u64 types = 0, memory = 0;
    for (u32 c = 0; c < k; c++) {
        fprintf(stderr, "   checker %2u: %9llu types %7.1f MB\n", c, (unsigned long long)per_checker_types[c], (double)per_checker_memory[c] / (1 << 20));
        types += per_checker_types[c], memory += per_checker_memory[c];
    }
    fprintf(stderr, "   total: %llu types, %.1f MB\n", (unsigned long long)types, (double)memory / (1 << 20));
    // Per checker: how much of what it made another checker made too (a shared graph's most for it), and every one
    for (u32 domain = DOMAIN_TYPE; domain <= DOMAIN_RELATION; domain += DOMAIN_RELATION - DOMAIN_TYPE) {
        u64 own[32] = {0}, shared[32] = {0}, everyone[32] = {0};
        for (u32 i = 0; i < census.cap; i++) {
            if (!census.keys[i] || (census.values[i] >> 56) != domain) continue;
            u32 mask = (u32)census.values[i];
            u32 n = (u32)__builtin_popcount(mask);
            for (u32 c = 0; c < k; c++) {
                if (!(mask >> c & 1)) continue;
                own[c]++;
                if (n >= 2) shared[c]++;
                if (n == k) everyone[c]++;
            }
        }
        u64 sum_own = 0, sum_shared = 0, sum_every = 0;
        for (u32 c = 0; c < k; c++) {
            fprintf(stderr, "   %s checker %2u: %9llu, also another's %5.1f%%, every checker's %5.1f%%\n",
                domain == DOMAIN_TYPE ? "types" : "relations", c, (unsigned long long)own[c],
                own[c] ? 100.0 * (double)shared[c] / (double)own[c] : 0.0, own[c] ? 100.0 * (double)everyone[c] / (double)own[c] : 0.0);
            sum_own += own[c], sum_shared += shared[c], sum_every += everyone[c];
        }
        fprintf(stderr, "   %s all checkers: also another's %5.1f%%, every checker's %5.1f%%\n", domain == DOMAIN_TYPE ? "types" : "relations",
            sum_own ? 100.0 * (double)sum_shared / (double)sum_own : 0.0, sum_own ? 100.0 * (double)sum_every / (double)sum_own : 0.0);
    }
#ifdef TTTT_ROOT_CENSUS
    root_census_report(k);
#endif
    print_domain(DOMAIN_TYPE, "types", k);
    print_domain(DOMAIN_TYPES_KEY, "types table keys", k);
    print_domain(DOMAIN_LINKS_KEY, "links table keys", k);
    print_domain(DOMAIN_NODE_LINK, "node links", k);
    print_domain(DOMAIN_RELATION, "relation cache", k);
}

#ifdef TTTT_ROOT_CENSUS
// The root census (a -DTTTT_ROOT_CENSUS build, run with TTTT_DUP_CENSUS=1; agents/notes/threading.md "The census"):
// what share of each checker's types made and check CPU is rooted at declarations two or more checkers reach, the
// question behind "share on second touch". A declaration is (file, line of the start of its span): the declaration
// span of a resolution entry (resolving_entry_span()) or of a type's home symbol. Each type made records the outermost
// and the innermost declaration on the resolution stack when it was made (the entries without a declaration, a base
// constraint or type arguments, are skipped); the check CPU of each file is split at the outermost declaration's
// push and pop. At the report a declaration is "hot" when two or more checkers (shares) touched it: resolved it at any
// depth, or made a type rooted at it under any rule
#include <time.h>

// outer (stack, else home), inner (stack, else home), home only, closure (home; a union or intersection by its
// members: hot when every member that isn't a literal or intrinsic has a hot home)
enum { RC_RULES = 4, RC_CLOSURE = 3 };

typedef struct RootState {
    Checker* checker;
    U64Map touched; // declaration keys this checker resolved -> when first
    U64Map cpu; // declaration key (0: no declaration open, the file's own check) -> thread CPU ns
} RootState;

static pthread_mutex_t rc_lock = PTHREAD_MUTEX_INITIALIZER;
static RootState* rc_states[4096];
static u32 rc_state_count;
static thread_local RootState* rc_cur;
static thread_local u64 rc_keys[1 << 16]; // Per resolution stack index: its declaration key (0 none)
static thread_local u32 rc_outer_depth; // 1 + the stack index of the outermost declaration, 0 if none is open
static thread_local u64 rc_mark;
static thread_local bool rc_in_file;
static u64* _Atomic rc_root[1 << LAZY_TYPE_DIR_BITS][3]; // Per type id: outer and inner declaration keys, when made (rc_clock())

static u64 rc_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (u64)t.tv_sec * 1000000000ull + (u64)t.tv_nsec;
}

// Monotonic ns, comparable across threads: which checker touched a declaration first
static u64 rc_clock(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)t.tv_sec * 1000000000ull + (u64)t.tv_nsec;
}

static u64 rc_span_key(Span span) {
    if (!span_valid(span)) return 0;
    SourceFile const* file = span_source(span);
    if (!file) return 0;
    u64 kind = file->is_stdlib ? 1 : file->is_declaration ? 2 : 3;
    return kind << 60 | (u64)(file->program_order + 1) << 24 | (span_line_index(span) & 0xffffff);
}

static u64 rc_symbol_key(Symbol const* symbol) {
    if (!symbol) return 0;
    if (symbol->decl_count) return rc_span_key(symbol->decls[0].full);
    return rc_span_key(symbol->name_span);
}

// The declaration a type comes from: its alias, the declaration it instantiates (ty_home()'s walk)
static u64 rc_home_key(Ty const* ty, u32 depth) {
    if (!ty || depth > 8) return 0;
    if (ty->alias) return rc_symbol_key(ty->alias);
    switch ((TyKind)ty->kind) {
        case TY_TYPE_PARAM: return rc_symbol_key(ty->param.symbol);
        case TY_DECLARED: return rc_symbol_key(ty->declared.symbol);
        case TY_REFERENCE: return rc_home_key(ty->reference.target, depth + 1);
        case TY_ANONYMOUS:
            if (ty->anonymous.function) return rc_span_key(function_error_span(ty->anonymous.function));
            if (ty->anonymous.target) return rc_home_key(ty->anonymous.target, depth + 1);
            // (A type literal or mapped type node: no span, its address (file kind unknown) stands for its declaration)
            if (ty->anonymous.node) return ((u64)(uintptr_t)ty->anonymous.node >> 3) & (((u64)1 << 56) - 1);
            if (ty->anonymous.object_literal && !ty->anonymous.overload_count && ty->anonymous.literal_expr) return rc_span_key(ty->anonymous.literal_expr->inner_span);
            if (!ty->anonymous.is_reverse && ty->anonymous.merged_value) return rc_symbol_key(ty->anonymous.merged_value);
            return 0;
        case TY_CONDITIONAL:
            if (ty->conditional.root && ty->conditional.root != ty) return rc_home_key(ty->conditional.root, depth + 1);
            return rc_home_key(ty->conditional.check, depth + 1);
        case TY_INDEXED: return rc_home_key(ty->indexed.object, depth + 1);
        case TY_KEYOF: return rc_home_key(ty->keyof.operand, depth + 1);
        case TY_SUBSTITUTION: return rc_home_key(ty->substitution.base, depth + 1);
        default: return 0;
    }
}

static RootState* rc_state(Checker* me) {
    if (rc_cur && rc_cur->checker == me) return rc_cur;
    pthread_mutex_lock(&rc_lock);
    RootState* found = nullptr;
    for (u32 i = 0; i < rc_state_count; i++) if (rc_states[i]->checker == me) found = rc_states[i];
    if (!found) {
        check(rc_state_count < c_arraylen(rc_states));
        found = calloc(1, sizeof(RootState));
        found->checker = me;
        rc_states[rc_state_count++] = found;
    }
    pthread_mutex_unlock(&rc_lock);
    return rc_cur = found;
}

static void rc_flush(RootState* state, u64 bucket) {
    u64 now = rc_now();
    if (rc_in_file) *map_find(&state->cpu, bucket | (u64)1 << 63, true, true, nullptr) += now - rc_mark;
    rc_mark = now;
}

void root_census_push(Checker* me) {
    if (!debug_env.dup_census) return;
    u32 at = my resolving_len - 1;
    check(at < c_arraylen(rc_keys));
    u64 key = rc_span_key(ty_resolving_entry_span(&my resolving[at]));
    rc_keys[at] = key;
    if (!key) return;
    RootState* state = rc_state(me);
    bool inserted;
    u64* first = map_find(&state->touched, key, true, true, &inserted);
    if (inserted) *first = rc_clock();
    if (!rc_outer_depth) {
        rc_flush(state, 0);
        rc_outer_depth = at + 1;
    }
}

void root_census_pop(Checker* me) {
    if (!debug_env.dup_census) return;
    u32 at = my resolving_len - 1;
    if (rc_outer_depth != at + 1) return;
    rc_flush(rc_state(me), rc_keys[at]);
    rc_outer_depth = 0;
}

void root_census_alloc(Checker* me, Ty const* ty) {
    if (!debug_env.dup_census) return;
    u32 index = ty->id >> LAZY_TYPE_CHUNK_BITS;
    u64 outer = 0, inner = 0;
    if (rc_outer_depth && rc_outer_depth <= my resolving_len) {
        outer = rc_keys[rc_outer_depth - 1];
        for (u32 i = my resolving_len; i-- > 0;) if (rc_keys[i]) { inner = rc_keys[i]; break; }
    }
    for (u32 r = 0; r < 3; r++) {
        // (A block's ids are one checker's: nobody else writes this chunk)
        u64* chunk = atomic_load_explicit(&rc_root[index][r], memory_order_relaxed);
        if (!chunk) {
            chunk = calloc((size_t)1 << LAZY_TYPE_CHUNK_BITS, sizeof(u64));
            panic_if(!chunk, "out of memory");
            atomic_store_explicit(&rc_root[index][r], chunk, memory_order_relaxed);
        }
        chunk[ty->id & ((1u << LAZY_TYPE_CHUNK_BITS) - 1)] = r == 2 ? rc_clock() : r ? inner : outer;
    }
}

void root_census_file(Checker* me, bool begin) {
    RootState* state = rc_state(me);
    if (begin) {
        rc_in_file = true;
        rc_mark = rc_now();
        rc_outer_depth = 0;
        return;
    }
    rc_flush(state, 0);
    rc_in_file = false;
}

// Merged per share at dup_census_add(): every type as (fingerprint, root per rule, share), every declaration's
// checker mask, the CPU per (share, declaration)
typedef struct RcType {
    u64 fp;
    u32 root[RC_RULES]; // Declaration indices (0: none, the file's own); closure: a composite's rc_leaves offset
    uint8_t share, kind;
    uint16_t leaves; // closure, a composite: its members' homes at root[RC_CLOSURE], 0xffff: a member has none
    bool composite, neutral; // closure: a union or intersection; a literal, an intrinsic or a composite of only those
} RcType;
static RcType* rc_types;
static u64 rc_types_len, rc_types_cap;
static U64Map rc_decl_index; // declaration key -> index
static u64* rc_decl_keys; // index -> key
static u32* rc_decl_mask; // index -> checker mask
static u64* rc_decl_first; // index -> when first touched (a resolution or a type rooted there), by rc_decl_first_share
static uint8_t* rc_decl_first_share;
static u32 rc_decl_len, rc_decl_cap;
static u64 rc_cpu[32][3]; // per share: ns rooted hot, cold, none (filled at the report from rc_cpu_raw)
typedef struct RcCpu { u32 share, decl; u64 ns; } RcCpu;
static RcCpu* rc_cpu_raw;
static u32 rc_decl(u64 key, u32 share, u64 when);
static u32* rc_leaf;
static u64 rc_leaf_len, rc_leaf_cap;

// A union's or intersection's members' homes (nested ones flattened), appended to rc_leaf; false if one has none
static bool rc_leaves(Ty const* ty, u32 depth, u32 share, u64 when) {
    if ((ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) && depth < 4) {
        for (u32 i = 0; i < ty->composite.types.len; i++) if (!rc_leaves(ty->composite.types.data[i], depth + 1, share, when)) return false;
        return true;
    }
    if (ty->kind == TY_INTRINSIC || ty->kind == TY_LITERAL) return true;
    u64 key = rc_home_key(ty, 0);
    if (!key) return false;
    if (rc_leaf_len == rc_leaf_cap) {
        rc_leaf_cap = rc_leaf_cap ? rc_leaf_cap * 2 : 1 << 20;
        rc_leaf = realloc(rc_leaf, sizeof(u32) * rc_leaf_cap);
        panic_if(!rc_leaf, "out of memory");
    }
    rc_leaf[rc_leaf_len++] = rc_decl(key, share, when);
    return true;
}
static u64 rc_cpu_len, rc_cpu_cap;

static u32 rc_decl(u64 key, u32 share, u64 when) {
    if (!key) return 0;
    bool inserted;
    u64* slot = map_find(&rc_decl_index, key, true, true, &inserted);
    if (inserted) {
        if (rc_decl_len + 1 >= rc_decl_cap) {
            rc_decl_cap = rc_decl_cap ? rc_decl_cap * 2 : 1 << 16;
            rc_decl_keys = realloc(rc_decl_keys, sizeof(u64) * rc_decl_cap);
            rc_decl_mask = realloc(rc_decl_mask, sizeof(u32) * rc_decl_cap);
            rc_decl_first = realloc(rc_decl_first, sizeof(u64) * rc_decl_cap);
            rc_decl_first_share = realloc(rc_decl_first_share, rc_decl_cap);
            panic_if(!rc_decl_keys || !rc_decl_mask || !rc_decl_first || !rc_decl_first_share, "out of memory");
            if (!rc_decl_len) rc_decl_len = 1; // (Index 0 is none)
        }
        rc_decl_keys[rc_decl_len] = key;
        rc_decl_mask[rc_decl_len] = 0;
        rc_decl_first[rc_decl_len] = UINT64_MAX;
        *slot = rc_decl_len++;
    }
    rc_decl_mask[*slot] |= 1u << share;
    if (when < rc_decl_first[*slot]) rc_decl_first[*slot] = when, rc_decl_first_share[*slot] = (uint8_t)share;
    return (u32)*slot;
}

static void root_census_add(Checker* me, u32 share) {
    RootState* state = nullptr;
    pthread_mutex_lock(&rc_lock);
    for (u32 i = 0; i < rc_state_count; i++) {
        if (rc_states[i]->checker != me) continue;
        state = rc_states[i];
        rc_states[i] = rc_states[--rc_state_count];
        break;
    }
    pthread_mutex_unlock(&rc_lock);
    if (rc_cur == state) rc_cur = nullptr;
    if (state) {
        for (u32 i = 0; i < state->touched.cap; i++) if (state->touched.keys[i]) rc_decl(state->touched.keys[i], share, state->touched.values[i]);
        for (u32 i = 0; i < state->cpu.cap; i++) {
            if (!state->cpu.keys[i]) continue;
            u64 key = state->cpu.keys[i] & ~((u64)1 << 63);
            if (rc_cpu_len == rc_cpu_cap) {
                rc_cpu_cap = rc_cpu_cap ? rc_cpu_cap * 2 : 1 << 12;
                rc_cpu_raw = realloc(rc_cpu_raw, sizeof(RcCpu) * rc_cpu_cap);
                panic_if(!rc_cpu_raw, "out of memory");
            }
            rc_cpu_raw[rc_cpu_len++] = (RcCpu){ share, rc_decl(key, share, UINT64_MAX), state->cpu.values[i] };
        }
        map_clear(&state->touched);
        map_clear(&state->cpu);
        free(state);
    }
    for (u32 o = 0; o < my lazy.own_len; o++) {
        u32 index = my lazy.own[o];
        Ty const* const* chunk = my lazy.types[index];
        if (!chunk) continue;
        u32 end = index == my next_type_id >> LAZY_TYPE_CHUNK_BITS ? (my next_type_id & ((1u << LAZY_TYPE_CHUNK_BITS) - 1)) : (1u << LAZY_TYPE_CHUNK_BITS);
        u64 const* outer = rc_root[index][0];
        u64 const* inner = rc_root[index][1];
        u64 const* made = rc_root[index][2];
        for (u32 i = index ? 0 : 1; i < end; i++) {
            Ty const* ty = chunk[i];
            if (!ty) continue;
            u64 fp = fp_ty(ty);
            if (!fp) continue;
            // (Types made before the census was on, the checker's own setup: as if first)
            u64 when = made ? made[i] : 0;
            u32 home = rc_decl(rc_home_key(ty, 0), share, when);
            u32 o_root = outer && outer[i] ? rc_decl(outer[i], share, when) : home;
            u32 i_root = inner && inner[i] ? rc_decl(inner[i], share, when) : home;
            if (rc_types_len == rc_types_cap) {
                rc_types_cap = rc_types_cap ? rc_types_cap * 2 : 1 << 20;
                rc_types = realloc(rc_types, sizeof(RcType) * rc_types_cap);
                panic_if(!rc_types, "out of memory");
            }
            RcType rec = { fp, { o_root, i_root, home, home }, (uint8_t)share, (uint8_t)(ty->kind < 15 ? ty->kind : 14), 0, false, false };
            if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
                u64 at = rc_leaf_len;
                rec.composite = true;
                bool all = rc_leaves(ty, 0, share, when);
                rec.root[RC_CLOSURE] = (u32)at;
                rec.leaves = !all ? 0xffff : rc_leaf_len - at > 0xfffe ? 0xffff : (uint16_t)(rc_leaf_len - at);
                rec.neutral = all && rc_leaf_len == at;
                if (!all) rc_leaf_len = at;
                panic_if(at > UINT32_MAX, "too many leaves");
            } else rec.neutral = ty->kind == TY_INTRINSIC || ty->kind == TY_LITERAL;
            rc_types[rc_types_len++] = rec;
        }
        // (The ids are reused by no one: free the chunks once read)
        for (u32 r = 0; r < 3; r++) {
            free(rc_root[index][r]);
            rc_root[index][r] = nullptr;
        }
    }
}

static bool rc_hot(u32 decl) {
    return decl && __builtin_popcount(rc_decl_mask[decl]) >= 2;
}

// A type's root under a rule: 0 none, 1 hot, 2 cold, 3 neutral (closure: literals, intrinsics); with `dynamic`, 4: hot
// but this checker touched the declaration first (the counter was 0: its derivations stay private)
static u32 rc_class(RcType const* type, u32 r, bool dynamic) {
    if (r == RC_CLOSURE) {
        if (type->neutral) return 3;
        if (type->composite) {
            if (type->leaves == 0xffff) return 0;
            bool first = false;
            for (u32 i = 0; i < type->leaves; i++) {
                u32 leaf = rc_leaf[type->root[r] + i];
                if (!rc_hot(leaf)) return 2;
                first |= rc_decl_first_share[leaf] == type->share;
            }
            return dynamic && first ? 4 : 1;
        }
    }
    u32 root = type->root[r];
    if (!root) return 0;
    if (!rc_hot(root)) return 2;
    return dynamic && rc_decl_first_share[root] == type->share ? 4 : 1;
}

static void root_census_report(u32 k) {
    static char const* const rule_names[RC_RULES] = { "outer", "inner", "home", "closure" };
    // Every type's holders, to tell which copies are duplicates
    U64Map holders = {0};
    for (u64 t = 0; t < rc_types_len; t++) *map_find(&holders, rc_types[t].fp, true, true, nullptr) |= 1u << rc_types[t].share;
    u64 dup_instances = 0;
    for (u64 t = 0; t < rc_types_len; t++) dup_instances += __builtin_popcount((u32)*map_find(&holders, rc_types[t].fp, false, true, nullptr)) >= 2;
    fprintf(stderr, "== root census: %u checkers, %llu types, %llu distinct (x%.3f), %llu of them made by 2+ checkers; %u declarations\n",
        k, (unsigned long long)rc_types_len, (unsigned long long)holders.len, holders.len ? (double)rc_types_len / holders.len : 0.0,
        (unsigned long long)dup_instances, rc_decl_len ? rc_decl_len - 1 : 0);
    u32 hot_decls = 0, hot_by_kind[4] = {0};
    for (u32 d = 1; d < rc_decl_len; d++) if (rc_hot(d)) hot_decls++, hot_by_kind[rc_decl_keys[d] >> 60 & 3]++;
    fprintf(stderr, "   hot declarations (2+ checkers): %u (lib %u, dts %u, ts %u)\n", hot_decls, hot_by_kind[1], hot_by_kind[2], hot_by_kind[3]);
    for (u32 r = 0; r < RC_RULES; r++) {
        // Per checker: types by class (none, hot, cold, neutral); over all: duplicates by class, hot fingerprints
        u64 per[32][4] = {0}, sum[4] = {0}, dup[4] = {0}, hot_by_kind_types[4] = {0};
        U64Map hot_fps = {0};
        for (u64 t = 0; t < rc_types_len; t++) {
            RcType const* type = &rc_types[t];
            u32 cls = rc_class(type, r, false);
            bool is_dup = __builtin_popcount((u32)*map_find(&holders, type->fp, false, true, nullptr)) >= 2;
            per[type->share][cls]++, sum[cls]++, dup[cls] += is_dup;
            if (cls == 1) {
                if (!(r == RC_CLOSURE && type->composite)) hot_by_kind_types[rc_decl_keys[type->root[r]] >> 60 & 3]++;
                map_find(&hot_fps, type->fp, true, false, nullptr);
            }
        }
        fprintf(stderr, "   rule %s: per checker types hot/cold/none/neutral %%:", rule_names[r]);
        for (u32 c = 0; c < k; c++) {
            u64 all = per[c][0] + per[c][1] + per[c][2] + per[c][3];
            fprintf(stderr, " %.0f/%.0f/%.0f/%.0f", all ? 100.0 * per[c][1] / all : 0, all ? 100.0 * per[c][2] / all : 0,
                all ? 100.0 * per[c][0] / all : 0, all ? 100.0 * per[c][3] / all : 0);
        }
        u64 all = sum[0] + sum[1] + sum[2] + sum[3];
        double pct = all ? 100.0 / all : 0, dpct = dup_instances ? 100.0 / dup_instances : 0;
        fprintf(stderr, "\n   rule %s: types hot %.1f%% cold %.1f%% none %.1f%% neutral %.1f%%; hot distinct/copies %.3f; duplicates by class hot %.1f%% cold %.1f%% none %.1f%% neutral %.1f%%; hot but single %.1f%% of all; hot by root lib/dts/ts %llu/%llu/%llu\n",
            rule_names[r], sum[1] * pct, sum[2] * pct, sum[0] * pct, sum[3] * pct, sum[1] ? (double)hot_fps.len / sum[1] : 0,
            dup[1] * dpct, dup[2] * dpct, dup[0] * dpct, dup[3] * dpct, (sum[1] - dup[1]) * pct,
            (unsigned long long)hot_by_kind_types[1], (unsigned long long)hot_by_kind_types[2], (unsigned long long)hot_by_kind_types[3]);
        // (A machine-readable line for scripts/sharedgraph/rootcensus.py: copies none hot cold neutral, duplicate copies
        // none hot cold neutral, distinct hot, all duplicates, all distinct)
        fprintf(stderr, "RC types %s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n", rule_names[r],
            (unsigned long long)sum[0], (unsigned long long)sum[1], (unsigned long long)sum[2], (unsigned long long)sum[3],
            (unsigned long long)dup[0], (unsigned long long)dup[1], (unsigned long long)dup[2], (unsigned long long)dup[3],
            (unsigned long long)hot_fps.len, (unsigned long long)dup_instances, (unsigned long long)holders.len);
        map_clear(&hot_fps);
        // The dynamic counter: copies kept private (cold, none, neutral, the first toucher's), copies shared and their
        // distinct ones
        u64 dyn_private = 0, dyn_shared = 0;
        U64Map dyn_fps = {0};
        for (u64 t = 0; t < rc_types_len; t++) {
            u32 cls = rc_class(&rc_types[t], r, true);
            if (cls == 1) dyn_shared++, map_find(&dyn_fps, rc_types[t].fp, true, false, nullptr);
            else dyn_private++;
        }
        fprintf(stderr, "   rule %s, dynamic: private %.1f%% (first touchers' %.1f%%), shared %.1f%% (distinct/copies %.3f)\n", rule_names[r],
            dyn_private * pct, (dyn_private - sum[0] - sum[2] - sum[3]) * pct, dyn_shared * pct, dyn_shared ? (double)dyn_fps.len / dyn_shared : 0);
        fprintf(stderr, "RC dyn %s %llu %llu %llu\n", rule_names[r], (unsigned long long)dyn_private, (unsigned long long)dyn_shared,
            (unsigned long long)dyn_fps.len);
        map_clear(&dyn_fps);
    }
    // The rootless types by kind (outer rule): copies, and how many of them are duplicates
    {
        u64 by_kind[16][2] = {0};
        for (u64 t = 0; t < rc_types_len; t++) {
            if (rc_types[t].root[0]) continue;
            by_kind[rc_types[t].kind][0]++;
            by_kind[rc_types[t].kind][1] += __builtin_popcount((u32)*map_find(&holders, rc_types[t].fp, false, true, nullptr)) >= 2;
        }
        fprintf(stderr, "   rootless by kind (copies/duplicates):");
        for (u32 i = 0; i < 15; i++) if (by_kind[i][0]) fprintf(stderr, " %s %llu/%llu", kind_names[i], (unsigned long long)by_kind[i][0], (unsigned long long)by_kind[i][1]);
        fprintf(stderr, "\n");
    }
    memset(rc_cpu, 0, sizeof(rc_cpu));
    for (u64 i = 0; i < rc_cpu_len; i++) {
        RcCpu const* entry = &rc_cpu_raw[i];
        rc_cpu[entry->share][!entry->decl ? 2 : rc_hot(entry->decl) ? 0 : 1] += entry->ns;
    }
    u64 total[3] = {0};
    fprintf(stderr, "   check CPU per checker hot/cold/none %%:");
    for (u32 c = 0; c < k; c++) {
        u64 all = rc_cpu[c][0] + rc_cpu[c][1] + rc_cpu[c][2];
        for (u32 j = 0; j < 3; j++) total[j] += rc_cpu[c][j];
        fprintf(stderr, " %.0f/%.0f/%.0f", all ? 100.0 * rc_cpu[c][0] / all : 0, all ? 100.0 * rc_cpu[c][1] / all : 0, all ? 100.0 * rc_cpu[c][2] / all : 0);
    }
    u64 all = total[0] + total[1] + total[2];
    fprintf(stderr, "\n   check CPU: %.1f ms, hot %.1f%% cold %.1f%% none (the files' own) %.1f%%\n", all / 1e6,
        all ? 100.0 * total[0] / all : 0, all ? 100.0 * total[1] / all : 0, all ? 100.0 * total[2] / all : 0);
    fprintf(stderr, "RC cpu %llu %llu %llu\n", (unsigned long long)total[0], (unsigned long long)total[1], (unsigned long long)total[2]);
    map_clear(&holders);
}
#endif
