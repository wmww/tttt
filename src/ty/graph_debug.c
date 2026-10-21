// --shared-graph's debug gates (plan 86 4f, scripts/sharedgraph/README.md): the fingerprint-at-publish oracle, compiled
// only with -DTTTT_GRAPH_FINGERPRINT (taint.h). With TTTT_GRAPH_FINGERPRINT set, program_typecheck() first checks
// every file with one private checker (no graph: a K1 history) while this file records the structural fingerprint of
// each result per key (record mode), then runs the shared workers, which compare every result they compute and publish
// with the record (check mode): a clean result is a pure function of other clean results, so whoever computes it must
// get K1's value; a difference is a publication that broke the rule (a tainted value, a read of something in flight).
// The keys are structural (fingerprint.inc: ids replaced by the referents' fingerprints) and the AST, symbols and
// declarations they hash by address are the same objects in both checks because both run in one process: that is
// why the record isn't a file another run reads (addresses differ between processes; the brief's file was dropped for
// it). What is fingerprinted:
// - the TAINT_END() results: members and declared raw members (key: the type), property types (the property),
//   signature returns (the signature), type parameter constraints and base constraints (the type), declared types of
//   symbols (the symbol), inferred return types (the function), variances (the type; VARIANCE_UNRELIABLE masked: it
//   varies run to run without the output moving, plan 86 4e item 1), declaration memos (the LazyType);
// - memo kinds of the intern tables that fingerprint.inc's fp_key() knows (anonymous and conditional instances,
//   indexed access, union pairs, definitely-related, keyof properties, normalized intersections, distributive
//   constraints, reduced unions, simplified, regular unions, widened objects): recorded from the K1 checker's tables
//   at its end, checked at graph_memo_flush()'s publication (winners and losers alike).
// Not fingerprinted: identities (the value is what the key is), the other memo kinds (fp_key() doesn't know their
// words: KEY_ALIAS_INSTANCE, KEY_UNION_NODE, KEY_BASE_SIGNATURE, KEY_ITERATION_TYPES, KEY_REVERSE_MAPPED...),
// TK_FREE_PARAMS, TK_SIMPLIFIED's frame (the KEY_SIMPLIFIED entry is), relation cache results, Ty-resident fields
// without a TAINT_END (conditional branches, mapped templates, deferred args, reduced intersections, equivalent bases,
// class base variables, signature resolution, implied constraints). A key whose K1 values disagree among themselves is
// ambiguous and skipped; one K1 never computed is counted as unrecorded.
// TTTT_GRAPH_FINGERPRINT=1: abort with a backtrace at the first difference; =count: count them all, print up to 5 per
// kind with their backtraces; =print adds the types printed (perturbs: printing resolves);
// TTTT_GRAPH_FINGERPRINT_TRACE=1 keeps each K1 value's fields to print beside a difference's (memory: zod ~1 GB more).
// A difference whose value fingerprinted afresh is K1's is a stale memo of type fingerprints, counted apart. A summary
// per kind on stderr at the end.
#ifdef TTTT_GRAPH_FINGERPRINT
#include "ty/ty_internal.h"
#include "ty/graph.h"
#include "error.h"
#include "scope.h"
#include "binding.h"
#include "source_file.h"
#include <execinfo.h>
#include <pthread.h>
#include <stdatomic.h>

static pthread_mutex_t owners_lock = PTHREAD_MUTEX_INITIALIZER;
struct Prop;
static uint64_t fp_prop_owner_locked(struct Prop const* prop);
#define FP_TLS thread_local
#define FP_LAZY_PARTS 0
#define FP_PROP_OWNER(prop) fp_prop_owner_locked(prop)
#include "ty/fingerprint.inc"

static u64 fp_prop_owner_locked(Prop const* prop) {
    pthread_mutex_lock(&owners_lock);
    u64* owner = map_find(&prop_owners, (u64)(uintptr_t)prop, false, true, nullptr);
    u64 ret = owner ? *owner : 0;
    pthread_mutex_unlock(&owners_lock);
    return ret;
}

enum { FP_OFF, FP_RECORD, FP_CHECK };
static int mode;
static int on_difference; // 0 abort, 1 count, 2 count and print types
static U64Map recorded; // (kind, key fingerprint) -> value fingerprint, written in record mode only
static U64Map ambiguous; // keys K1 gave two values
static bool keep_traces; // TTTT_GRAPH_FINGERPRINT_TRACE=1: the K1 values' fields kept, printed beside a difference's
static U64Map traces; // key -> strdup'd fields

typedef struct FpStats {
    _Atomic u64 recorded, ambiguous, checked, matched, unrecorded, skipped, differ, stale, unfingerprinted;
} FpStats;
static FpStats stats[TK_COUNT];

static char const* kind_label(unsigned kind, char* buf, size_t size) {
    static char const* const names[] = {
        "members", "declared raw members", "property type", "signature return", "type parameter constraint",
        "symbol declared type", "inferred return type", "base constraint", "simplified", "variances", "free params",
    };
    if (kind >= TK_MEMBERS && kind < TK_MEMBERS + c_arraylen(names)) return names[kind - TK_MEMBERS];
    if (kind >= TK_LAZY_DECL && kind < TK_LAZY_EXPR) snprintf(buf, size, "declaration memo (tag %u)", kind - TK_LAZY_DECL);
    else snprintf(buf, size, "%s", graph_kind_name(kind));
    return buf;
}

void graph_fp_start(bool record) {
    char const* env = getenv("TTTT_GRAPH_FINGERPRINT");
    on_difference = env && !strcmp(env, "print") ? 2 : env && !strcmp(env, "count") ? 1 : 0;
    // (Each thread's memo of type fingerprints is its checker's: the record's types are the K1 checker's)
    map_clear(&ty_fps);
    keep_traces = getenv("TTTT_GRAPH_FINGERPRINT_TRACE") != nullptr;
    mode = record ? FP_RECORD : FP_CHECK;
}

bool graph_fp_on(void) {
    return mode != FP_OFF;
}

// A result's fingerprint by its kind (the key is the caller's)
static u64 value_of(unsigned kind, void const* result) {
    if (kind < 160) return fp_value(kind, result);
    switch (kind) {
        case TK_MEMBERS:
        case TK_DECLARED_RAW:
            return fp_members(result);
        case TK_VARIANCES: {
            struct { u32 len; unsigned char data[]; } const* v = result;
            unsigned char masked[256];
            u32 len = v->len < sizeof masked ? v->len : sizeof masked;
            for (u32 i = 0; i < len; i++) masked[i] = v->data[i] & ~8u;
            return fp_bytes(masked, len);
        }
        default:
            return fp_ty(result);
    }
}

// The value fingerprinted again without the memo of type fingerprints, its fields in *out if not null (fingerprint.inc's
// trace): a memoized fingerprint taken before a lazily resolved part was done can differ from a fresh one
static u64 value_fresh(unsigned kind, void const* result, cstr* out) {
    U64Map saved = ty_fps;
    ty_fps = (U64Map){0};
    trace = out;
    trace_depth = 0;
    u64 ret = value_of(kind, result);
    trace = nullptr;
    map_clear(&ty_fps);
    ty_fps = saved;
    return ret;
}

static void describe(Checker* c, unsigned kind, void const* subject, void const* result) {
    if (kind == TK_PROP_TYPE) eprintln("fingerprint:   property ", atom_sv(((Prop const*)subject)->name));
    if (kind == TK_SYMBOL_DECLARED) eprintln("fingerprint:   symbol ", ((Symbol const*)subject)->name);
    if (on_difference < 2) return;
    bool subject_type = kind == TK_MEMBERS || kind == TK_DECLARED_RAW || kind == TK_PARAM_CONSTRAINT ||
        kind == TK_BASE_CONSTRAINT || kind == TK_VARIANCES;
    if (subject_type && subject) {
        cstr s = ty_to_cstr(c, subject);
        eprintln("fingerprint:   subject ", cstr_sv(&s));
        cstr_drop(&s);
    }
    bool result_type = kind != TK_MEMBERS && kind != TK_DECLARED_RAW && kind != TK_VARIANCES &&
        (kind >= 160 || (kind != KEY_CONDITIONAL_INSTANCE && kind != KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT && kind != KEY_DEFINITELY_RELATED));
    if (result_type && result) {
        cstr r = ty_to_cstr(c, result);
        eprintln("fingerprint:   result ", cstr_sv(&r));
        cstr_drop(&r);
    }
}

static void note(unsigned kind, u64 key, u64 value, void const* subject, void const* result) {
    u64 slot = fp_mix(key, kind) | 1;
    FpStats* s = &stats[kind < TK_COUNT ? kind : TK_COUNT - 1];
    if (mode == FP_RECORD) {
        bool inserted;
        u64* known = map_find(&recorded, slot, true, true, &inserted);
        if (inserted) {
            *known = value, s->recorded++;
            if (keep_traces) {
                cstr fields = cstr_init();
                value_fresh(kind, result, &fields);
                *map_find(&traces, slot, true, true, nullptr) = (u64)(uintptr_t)strdup(cstr_str(&fields));
                cstr_drop(&fields);
            }
        }
        else if (*known != value && !map_find(&ambiguous, slot, false, false, nullptr)) {
            map_find(&ambiguous, slot, true, false, nullptr);
            s->ambiguous++;
        }
        return;
    }
    atomic_fetch_add_explicit(&s->checked, 1, memory_order_relaxed);
    u64* known = map_find(&recorded, slot, false, true, nullptr);
    if (!known) {
        atomic_fetch_add_explicit(&s->unrecorded, 1, memory_order_relaxed);
        return;
    }
    if (ambiguous.len && map_find(&ambiguous, slot, false, false, nullptr)) {
        atomic_fetch_add_explicit(&s->skipped, 1, memory_order_relaxed);
        return;
    }
    if (*known == value) {
        atomic_fetch_add_explicit(&s->matched, 1, memory_order_relaxed);
        return;
    }
    // (Fingerprinted again from scratch: if that is K1's, the memo of type fingerprints was stale, not the result)
    if (value_fresh(kind, result, nullptr) == *known) {
        atomic_fetch_add_explicit(&s->stale, 1, memory_order_relaxed);
        return;
    }
    u64 n = atomic_fetch_add_explicit(&s->differ, 1, memory_order_relaxed);
    if (on_difference && n >= 5) return;
    static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&print_lock);
    Checker* c = current_checker;
    char buf[48];
    eprintln("fingerprint: differs from K1: ", kind_label(kind, buf, sizeof buf), " key ", key, " K1 ", *known, " now ", value,
        " (worker ", c ? (u32)c->graph_worker : 0u, ", checking ",
        c && c->checking_file ? cstr_str(&c->checking_file->canonical_path) : "(no file)", ")");
    describe(c, kind, subject, result);
    if (keep_traces) {
        u64* first = map_find(&traces, slot, false, true, nullptr);
        cstr fields = cstr_init();
        value_fresh(kind, result, &fields);
        eprintln("fingerprint:   K1  fields", first ? (char const*)(uintptr_t)*first : " ?");
        eprintln("fingerprint:   now fields", cstr_sv(&fields));
        cstr_drop(&fields);
    }
    // (scripts/sharedgraph/fingerprint.sh symbolizes the frames with addr2line)
    void* frames[48];
    int count = backtrace(frames, 48);
    backtrace_symbols_fd(frames, count, 2);
    if (!on_difference) {
        fflush(stderr);
        abort();
    }
    pthread_mutex_unlock(&print_lock);
}

void graph_fp_result(unsigned kind, void const* subject, void const* result) {
    if (__builtin_expect(mode == FP_OFF, 1)) return;
    Checker* c = current_checker;
    if (!c || kind >= TK_COUNT) return;
    if ((kind == TK_MEMBERS || kind == TK_DECLARED_RAW) && result) {
        // (Properties without an identity of their own go by their first owner's, fingerprint.inc's fp_prop())
        Members const* members = result;
        u64 owner = 0;
        for (u32 i = 0; i < members->prop_count; i++) {
            if (!owner) owner = fp_ty(subject);
            pthread_mutex_lock(&owners_lock);
            bool inserted;
            u64* slot = map_find(&prop_owners, (u64)(uintptr_t)members->props[i], true, true, &inserted);
            if (inserted) *slot = fp_mix(owner, i) | 1;
            pthread_mutex_unlock(&owners_lock);
        }
    }
    u64 key = 0, value = 0;
    switch (kind) {
        case TK_MEMBERS:
        case TK_DECLARED_RAW:
            key = fp_ty(subject);
            value = fp_members(result);
            if (!value) key = 0;
            break;
        case TK_PROP_TYPE:
            key = fp_prop(subject, 0);
            value = fp_ty(result);
            break;
        case TK_SIGNATURE_RETURN:
            key = fp_signature(subject, 0);
            value = fp_ty(result);
            break;
        case TK_PARAM_CONSTRAINT:
        case TK_BASE_CONSTRAINT:
            key = fp_ty(subject);
            value = fp_ty(result);
            break;
        case TK_VARIANCES:
            key = fp_ty(subject);
            value = value_of(kind, result);
            break;
        case TK_SYMBOL_DECLARED:
        case TK_FUNCTION_RETURN:
            key = (u64)(uintptr_t)subject;
            value = fp_ty(result);
            break;
        default:
            if (kind >= TK_LAZY_DECL && kind < TK_LAZY_EXPR) {
                key = (u64)(uintptr_t)subject;
                value = fp_ty(result);
            }
            break;
    }
    if (!key) {
        atomic_fetch_add_explicit(&stats[kind].unfingerprinted, 1, memory_order_relaxed);
        return;
    }
    note(kind, key, value, subject, result);
}

// A memo kind's intern entry (its block: the value, then the key's words) with its value
static void memo_entry(Checker* c, u32 kind, void const** block, u64 const* words, u32 count) {
    u64 key = fp_key(c, kind, words, count);
    if (!key) return;
    note(kind, key, fp_value(kind, block[0]), block, block[0]);
}

void graph_fp_memo(u32 kind, void const** block, u64 const* words, u32 count) {
    if (mode != FP_CHECK) return;
    Checker* c = current_checker;
    if (c && kind < 160) memo_entry(c, kind, block, words, count);
}

static void record_table(Checker* c, InternTable const* table) {
    for (u32 i = 0; i < table->cap; i++) {
        InternEntry const* entry = &table->entries[i];
        if (!entry->hash || !entry->block || !entry->block[0]) continue;
        u32 count = (u32)(entry->hash >> 48);
        u64 const* words = (u64 const*)(entry->block + 1);
        if (count == 0xffffu) count = (u32)*words++;
        if (!count) continue;
        u32 kind = (u32)(words[0] & 255);
        if (kind >= 160 || graph_kind_class(kind) != GRAPH_MEMO) continue;
        memo_entry(c, kind, entry->block, words, count);
    }
}

// The end of the record: the K1 checker's memo entries
void graph_fp_record_tables(Checker* c) {
    record_table(c, &c->types);
    record_table(c, &c->links);
}

void graph_fp_print(void) {
    if (mode == FP_OFF) return;
    u64 differ = 0;
    for (u32 kind = 0; kind < TK_COUNT; kind++) {
        FpStats* s = &stats[kind];
        if (!s->recorded && !s->checked && !s->unfingerprinted) continue;
        char buf[48];
        eprint("fingerprint: ", kind_label(kind, buf, sizeof buf), ": recorded ", (u64)s->recorded, " (ambiguous ",
            (u64)s->ambiguous, "), checked ", (u64)s->checked, ": matched ", (u64)s->matched);
        eprintln(", differ ", (u64)s->differ, " (and ", (u64)s->stale, " a stale memo's), unrecorded ", (u64)s->unrecorded,
            ", ambiguous ", (u64)s->skipped, "; unfingerprinted ", (u64)s->unfingerprinted);
        differ += s->differ;
    }
    eprintln("fingerprint: ", differ, " differences from K1");
}

#else
// (Nothing without TTTT_GRAPH_FINGERPRINT: ISO C wants something in a translation unit)
typedef int graph_debug_unused;
#endif
