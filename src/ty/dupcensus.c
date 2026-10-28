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
    print_domain(DOMAIN_TYPE, "types", k);
    print_domain(DOMAIN_TYPES_KEY, "types table keys", k);
    print_domain(DOMAIN_LINKS_KEY, "links table keys", k);
    print_domain(DOMAIN_NODE_LINK, "node links", k);
    print_domain(DOMAIN_RELATION, "relation cache", k);
}
