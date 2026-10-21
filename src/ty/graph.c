// --shared-graph: the type graph checkers share, see graph.h and APPROACH.md
#include "ty/graph.h"
#include "ty/ty_internal.h"
#include "lazy_type.h"
#include "program.h"
#include "ty/shared_table.h"
#include <pthread.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <time.h>
#include <sched.h>


typedef struct KindStats {
    u64 shared_hits; // Lookups a checker's own table missed and the shared one had
    u64 published; // Entries published
    u64 tainted; // Entries of memo kinds kept private because their computation was tainted
    u64 scratch; // Entries of scratch kinds (never published)
    u64 unfinished; // Entries without a value when their checker's share ended
    u64 lost; // Identities another checker published first (intern_publish())
} KindStats;

// What a checker made, by GraphClass and by kind (256: node_links kinds 256 + kind)
enum { GRAPH_STAT_KINDS = 512, GRAPH_STAT_MEMOS = 500 };

struct TyGraph {
    bool concurrent; // On several threads (graph_set_concurrent())
    // Workers retired by a redo (graph_retire()): their in-flight fields are nobody's, whoever finds one resets it
    _Atomic bool retired[64];
    _Atomic u32 redos; // Files checked again after a broken wait cycle (TTTT_GRAPH_STATS)
    pthread_mutex_t adopt_lock; // graph_adopt() on a worker thread (a redo's fresh checker)
    TySingletons* singletons; // The lib singletons, published once (ty.h)
    // The first checker's, which every other one shares (checker_new() makes them only for the first)
    Ty const* intrinsics[TY_INTRINSIC_COUNT];
    Ty const* typeof_type;
    struct Atom const* atom_constructor;
    struct Atom const* atom_prototype;
    struct Atom const* atom_bind;
    // The shared tables
    SharedTable* types;
    SharedTable* links;
    SharedTable* intersection_props;
    SharedLinks* node_links;
    SharedLinks* variance_slots; // By subject (graph_variance_slot())
    unsigned variance_lock; // The worker measuring variances, owner-tagged (graph_variance_lock())
    // Declaration memos by slot, like LazySlots.top (pages made by CAS, a memo written flags last under memo_lock)
    LazyMemo** _Atomic memos[1 << LAZY_TOP_BITS];
    pthread_mutex_t memo_lock;
    u64 memo_hits;
    KindStats stats[GRAPH_STAT_KINDS];
    u32 checkers;
    Checker* workers[64]; // By Checker.graph_worker
    // Who waits for whom (lazy_wait_slow()): a worker's number -> the worker whose field it waits for, 0 if none. Read
    // and written under wait_lock, only when a worker meets another's field in flight
    uint8_t waiting_on[64];
    pthread_mutex_t wait_lock;
};


static GraphClass graph_kind_class_of(u32 kind);
// (Debug: TTTT_GRAPH_NOSHARE=k,k,... or =all-but:k,k: kinds kept private, a bisection of what sharing breaks)
static uint8_t noshare_kinds[256];
static int noshare_init = -1;
GraphClass graph_kind_class(u32 kind) {
    if (__builtin_expect(noshare_init < 0, 0)) {
        char const* env = getenv("TTTT_GRAPH_NOSHARE");
        if (env) {
            bool but = !strncmp(env, "all-but:", 8);
            if (but) { memset(noshare_kinds, 1, sizeof(noshare_kinds)); env += 8; }
            for (char const* p = env; *p;) {
                char* end;
                unsigned long k = strtoul(p, &end, 10);
                if (end == p) break;
                if (k < 256) noshare_kinds[k] = !but;
                p = *end == ',' ? end + 1 : end;
            }
        }
        noshare_init = 1;
    }
    GraphClass ret = graph_kind_class_of(kind);
    return ret != GRAPH_SCRATCH && kind < 256 && noshare_kinds[kind] ? GRAPH_SCRATCH : ret;
}

static GraphClass graph_kind_class_of(u32 kind) {
    switch (kind) {
    // Roots (per symbol, per declaration) and identities (constructors: the value is what the key is)
    case KEY_TYPE_PARAM: case KEY_SYMBOL_LINKS: case KEY_GENERIC_PARAMS: case KEY_NAMESPACE: case KEY_TYPE_ONLY_EXPORT:
    case KEY_MERGED_VALUE: case KEY_DECL_GROUP: case KEY_DECL_GROUP_OF: case KEY_PRIVATE_NAME:
    case KEY_PRIVATE_NAME_CLASS: case KEY_NAMESPACE_IMPORT: case KEY_CLASS_MEMBER_SYMBOLS:
    case KEY_LITERAL: case KEY_UNION: case KEY_INTERSECTION: case KEY_REFERENCE: case KEY_TUPLE_TARGET: case KEY_KEYOF:
    case KEY_INDEXED: case KEY_CONDITIONAL: case KEY_SUBSTITUTION: case KEY_ANONYMOUS_INSTANCE: case KEY_UNIQUE_SYMBOL:
    case KEY_TEMPLATE_LITERAL: case KEY_STRING_MAPPING: case KEY_NODE: case KEY_DEFERRED_OUTER:
    case KEY_DEFERRED_REFERENCE: case KEY_STRUCTURAL: case KEY_RESTRICTIVE_PARAM: case KEY_MARKER: case KEY_CHECK_MARKER:
    case KEY_FUNCTION: case KEY_UNIQUE_SYMBOL_NAME: case KEY_FUNCTION_RETURN: case KEY_INSTANTIATION_QUERY:
    case KEY_IMPLEMENTATION_SIGNATURE: case KEY_SIGNATURE_INSTANCE: case KEY_SIGNATURE_TYPE:
    case KEY_MODULE_CLONE: case KEY_FRESH_LITERAL:
    // (A property's own: whoever sees the property may look for them, so they go out before it does)
    case KEY_PROP_MAPPED_TYPE: case KEY_SYNTHETIC_PROP_PARTS: case KEY_PROP_WRITE_TYPE:
        return GRAPH_IDENTITY;
    // Computed values (some of them computed while lowering or instantiating: KEY_ALIAS_INSTANCE, KEY_UNION_NODE,
    // the conditional instances)
    case KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT: case KEY_CONDITIONAL_INSTANCE: case KEY_ALIAS_INSTANCE:
    case KEY_UNION_NODE: case KEY_ALIAS_VARIANCES:
    case KEY_REGULAR_UNION: case KEY_WIDENED_OBJECTS: case KEY_REGULAR_OBJECT_LITERAL: case KEY_MAPPED_KEY_PARAM:
    case KEY_KEYOF_PROPERTIES: case KEY_SIMPLIFIED: case KEY_NORMALIZED_INTERSECTION: case KEY_MAPPED_FILTERING:
    case KEY_UNION_ACCESS: case KEY_BASE_CONSTRAINT:
    case KEY_INFERRED_TRUE_BRANCH: case KEY_DISTRIBUTION_DEPENDENT: case KEY_DISTRIBUTIVE_CONSTRAINT:
    case KEY_IMPLIED_CONSTRAINT: case KEY_TYPE_VARIABLE_REDUCTION: case KEY_DEFINITELY_RELATED:
    case KEY_CANONICAL_SIGNATURE: case KEY_BASE_SIGNATURE: case KEY_VARIANCES: case KEY_ALIAS_MARKER:
    case KEY_CHECK_MARKER_REF: case KEY_ITERATION_TYPES: case KEY_WIDENED_NULLABLE:
    case KEY_INTERSECTION_PROP_CONTEXT: case KEY_UNION_PAIR:
    case KEY_INTERSECTION_OF: case KEY_REVERSE_MAPPED: case KEY_UNION_KEY_NAME: case KEY_UNION_KEY_MEMBER: case KEY_FLOW_NOT_UNIT:
    case KEY_EVOLVING_ARRAY: case KEY_REDUCED_UNION: case KEY_FUNCTION_FREE_PARAMS: case KEY_FUNCTION_TYPE_MENTIONS:
    case KEY_ARRAY_THIS_PROPS: case KEY_TUPLE_NAME_ORDER: case KEY_INDEX_NAME_ORDER: case KEY_DECIMAL_ORDER:
    case KEY_THISLESS_SIGNATURES: case GRAPH_KIND_INTERSECTION_PROPS:
        return GRAPH_MEMO;
    // KEY_GENERIC_RELATION numbers relations per checker; KEY_UNDEFINED_PROPERTY is a first-need cache whose candidates
    // print differently (shared, which came first would be the schedule's); the rest is per expression, per call, flow,
    // inference, report-once
    default:
        return GRAPH_SCRATCH;
    }
}

static void graph_link_tables(TyGraph* me, Checker* checker) {
    checker->graph = me;
    lazy_slots_share(&checker->lazy);
    checker->types.shared = my types;
    checker->links.shared = my links;
    checker->intersection_props.shared = my intersection_props;
}

void graph_publish(TyGraph* me, Checker* checker);
static void publish_declaration_memo(TyGraph* me, Checker* checker, u32 slot);

TyGraph* graph_new(Checker* first) {
    TyGraph* me = calloc(1, sizeof(TyGraph));
    panic_if(!me, "out of memory");
    my types = shared_table_new();
    my links = shared_table_new();
    my intersection_props = shared_table_new();
    my node_links = shared_links_new();
    my variance_slots = shared_links_new();
    pthread_mutex_init(&my memo_lock, nullptr);
    memcpy(my intrinsics, first->intrinsics, sizeof(my intrinsics));
    my typeof_type = first->typeof_type;
    my atom_constructor = first->atom_constructor;
    my atom_prototype = first->atom_prototype;
    my atom_bind = first->atom_bind;
    graph_link_tables(me, first);
    my singletons = ty_singletons_new();
    first->singletons = my singletons;
    ty_singletons_sync(first);
    my checkers = 1;
    pthread_mutex_init(&my wait_lock, nullptr);
    pthread_mutex_init(&my adopt_lock, nullptr);
    first->graph_worker = 1;
    my workers[1] = first;
    // (What it made before the graph, the intrinsics' literals and typeof's union among them, is everyone's from the start:
    // on threads the other checkers don't wait for its share's end)
    graph_publish(me, first);
    return me;
}

void graph_adopt(TyGraph* me, Checker* checker) {
    memcpy(checker->intrinsics, my intrinsics, sizeof(my intrinsics));
    checker->typeof_type = my typeof_type;
    checker->atom_constructor = my atom_constructor;
    checker->atom_prototype = my atom_prototype;
    checker->atom_bind = my atom_bind;
    pthread_mutex_lock(&my adopt_lock);
    graph_link_tables(me, checker);
    checker->singletons = my singletons;
    ty_singletons_sync(checker);
    my checkers++;
    panic_if(my checkers > 63, "--shared-graph: at most 63 checkers");
    checker->graph_worker = (uint8_t)my checkers;
    // (Memo publication from the second checker on: the first one's, made alone, went with graph_new()'s publication)
    if (graph_memo_eager()) {
        checker->graph_memo_on = true;
        if (my workers[1]) my workers[1]->graph_memo_on = true;
    }
    __atomic_store_n(&my workers[my checkers], checker, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&my adopt_lock);
}

bool graph_can_redo(TyGraph const* me) {
    static int off = -1;
    if (off < 0) off = getenv("TTTT_GRAPH_REDO") && !strcmp(getenv("TTTT_GRAPH_REDO"), "0");
    return !off && my concurrent && __atomic_load_n(&((TyGraph*)me)->checkers, __ATOMIC_RELAXED) < 60;
}

void graph_retire(TyGraph* me, Checker* checker) {
    atomic_store(&my retired[checker->graph_worker], true);
    atomic_fetch_add(&my redos, 1);
}

void graph_set_concurrent(TyGraph* me, bool concurrent) {
    my concurrent = concurrent;
}

// (Nothing is handed over any more: atoms and ids are the program's (4c), the singletons the graph's (TySingletons),
// and a checker's id table is told of every chunk (lazy_slots_share()))
void graph_enter(TyGraph* me, Checker* checker) {
    (void)me, (void)checker;
}

void graph_release(TyGraph* me, Checker* checker) {
    if (checker->graph_worker < 64 && my workers[checker->graph_worker] == checker) my workers[checker->graph_worker] = nullptr;
    // (The graph's: freed by graph_drop())
    free(checker->graph_memo_slots);
    free(checker->graph_memo_log);
    free(checker->graph_variance_claims);
    checker->graph_variance_claims = nullptr;
    checker->graph_variance_claims_len = checker->graph_variance_claims_cap = 0;
    checker->graph_memo_log = nullptr;
    checker->graph_memo_log_len = checker->graph_memo_log_cap = 0;
    checker->graph_memo_slots = nullptr;
}

void graph_drop(TyGraph* me) {
    if (!me) return;
    shared_table_drop(my types);
    shared_table_drop(my links);
    shared_table_drop(my intersection_props);
    shared_links_drop(my node_links);
    shared_links_drop(my variance_slots);
    pthread_mutex_destroy(&my memo_lock);
    pthread_mutex_destroy(&my wait_lock);
    for (size_t i = 0; i < c_arraylen(my memos); i++) {
        if (!my memos[i]) continue;
        for (size_t j = 0; j < (1 << LAZY_DIR_BITS); j++) free(my memos[i][j]);
        free(my memos[i]);
    }
    free(me);
}

// Statistics

void graph_count_shared_hit(InternTable const* table, u64 const* words, u32 count) {
    Checker* checker = current_checker;
    if (!checker || !checker->graph || !count) return;
    u32 kind = table == &checker->intersection_props ? GRAPH_KIND_INTERSECTION_PROPS : (u32)(words[0] & 255);
    __atomic_fetch_add(&checker->graph->stats[kind].shared_hits, 1, __ATOMIC_RELAXED);
}

#ifdef TTTT_GRAPH_TAINT
// The cheap taint (taint.h's non-TTTT_TAINT macros): the counter and the pending puts per thread, the marks under a lock

thread_local u64 graph_taint_now;
thread_local u32 graph_taint_pending;
bool graph_taint_marks;
#ifdef TTTT_TAINT
// (The -DTTTT_TAINT prototype, taint.c, has its own events: this one's only have to compile there)
static inline void graph_taint_tick(void) { graph_taint_now++; }
#endif

// Tainted results: intern blocks by address, other results by (kind, object) as taint.c keys them. A bitmap in front
// keeps the reads cheap
typedef struct MarkSet {
    u64* keys;
    u32 cap, len;
} MarkSet;
static MarkSet marks;
static pthread_mutex_t marks_lock = PTHREAD_MUTEX_INITIALIZER;
u64 graph_mark_bits[1 << 14]; // 2^20 bits

static u64 mix_key(u64 h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    return h;
}

static u64 object_key(unsigned kind, void const* object) {
    return (u64)(uintptr_t)object ^ (u64)kind << 48;
}

static bool mark_has_locked(u64 key) {
    if (!marks.cap) return false;
    u64 h = mix_key(key);
    for (u32 i = (u32)(h >> 20) & (marks.cap - 1);; i = (i + 1) & (marks.cap - 1)) {
        if (marks.keys[i] == key) return true;
        if (!marks.keys[i]) return false;
    }
}

static bool mark_has(u64 key) {
    u64 h = mix_key(key);
    if (!(__atomic_load_n(&graph_mark_bits[(h >> 6) & (c_arraylen(graph_mark_bits) - 1)], __ATOMIC_RELAXED) >> (h & 63) & 1)) return false;
    pthread_mutex_lock(&marks_lock);
    bool ret = mark_has_locked(key);
    pthread_mutex_unlock(&marks_lock);
    return ret;
}

static void mark_add(u64 key) {
    if (!key) return;
    pthread_mutex_lock(&marks_lock);
    if (mark_has_locked(key)) {
        pthread_mutex_unlock(&marks_lock);
        return;
    }
    if ((marks.len + 1) * 2 > marks.cap) {
        MarkSet bigger = { .cap = marks.cap ? marks.cap * 2 : 1024, .len = marks.len };
        bigger.keys = calloc(bigger.cap, sizeof(u64));
        panic_if(!bigger.keys, "out of memory");
        for (u32 i = 0; i < marks.cap; i++) {
            if (!marks.keys[i]) continue;
            u32 j = (u32)(mix_key(marks.keys[i]) >> 20) & (bigger.cap - 1);
            while (bigger.keys[j]) j = (j + 1) & (bigger.cap - 1);
            bigger.keys[j] = marks.keys[i];
        }
        free(marks.keys);
        marks = bigger;
    }
    u64 h = mix_key(key);
    u32 i = (u32)(h >> 20) & (marks.cap - 1);
    while (marks.keys[i]) i = (i + 1) & (marks.cap - 1);
    marks.keys[i] = key;
    marks.len++;
    __atomic_or_fetch(&graph_mark_bits[(h >> 6) & (c_arraylen(graph_mark_bits) - 1)], 1ull << (h & 63), __ATOMIC_RELAXED);
    __atomic_store_n(&graph_taint_marks, true, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&marks_lock);
}

// Tainted results by TK_* / KEY_* kind (taint.h), and those on a type an earlier checker made (shared Ty-resident
// results the next checker reads as they are: what 4a's overlays would keep apart)
static u64 tainted_by_kind[TK_COUNT];
static u64 tainted_shared_by_kind[TK_COUNT];
static u64 taint_events_seen;

// A private miss: where a computation of its value may start
typedef struct MissRecord {
    void const* table;
    u64 hash;
    u64 start;
} MissRecord;
#define MISS_RING 64
static thread_local MissRecord miss_ring[MISS_RING];
static thread_local u32 miss_next;

void graph_taint_miss(InternTable const* table, u64 hash) {
    miss_ring[miss_next++ % MISS_RING] = (MissRecord){ table, hash, graph_taint_now };
}

// The memo kinds whose own frames decide (TAINT_END), or that hold a placeholder while computed
static bool explicit_kind(u32 kind) {
    switch (kind) {
    case KEY_SIMPLIFIED: case KEY_VARIANCES: case KEY_ALIAS_VARIANCES: case KEY_FUNCTION_TYPE_MENTIONS:
        return true;
    default:
        return false;
    }
}

typedef struct PendingPut {
    void const* const* block;
} PendingPut;
static thread_local PendingPut* pending;
static thread_local u32 pending_cap;

void graph_taint_put(InternTable const* table, u64 hash, void const** block, u32 kind) {
    Checker* checker = current_checker;
    if (checker && table == &checker->intersection_props) kind = GRAPH_KIND_INTERSECTION_PROPS;
    if (graph_kind_class(kind) != GRAPH_MEMO || explicit_kind(kind)) return;
    u64 start = graph_taint_now;
    for (u32 i = 0; i < MISS_RING; i++) {
        MissRecord* m = &miss_ring[(miss_next - 1 - i) % MISS_RING];
        if (m->hash == hash && m->table == table) {
            start = m->start;
            m->hash = 0;
            break;
        }
    }
    if (start != graph_taint_now) {
        mark_add((u64)(uintptr_t)block);
        __atomic_fetch_add(&tainted_by_kind[kind], 1, __ATOMIC_RELAXED);
        return;
    }
    if (graph_taint_pending == pending_cap) {
        // (The ones with their value are done, before any event since: clean)
        u32 kept = 0;
        for (u32 i = 0; i < graph_taint_pending; i++) {
            if (!pending[i].block[0]) pending[kept++] = pending[i];
        }
        graph_taint_pending = kept;
        if (!pending_cap || kept * 2 > pending_cap) {
            pending_cap = pending_cap ? pending_cap * 2 : 4096;
            pending = realloc(pending, sizeof(PendingPut) * pending_cap);
            panic_if(!pending, "out of memory");
        }
    }
    pending[graph_taint_pending++] = (PendingPut){ block };
}

// An event: the entries put that still have no value were in flight across it
void graph_taint_sweep(void) {
    __atomic_fetch_add(&taint_events_seen, 1, __ATOMIC_RELAXED);
    for (u32 i = 0; i < graph_taint_pending; i++) {
        if (!pending[i].block[0]) mark_add((u64)(uintptr_t)pending[i].block);
    }
    graph_taint_pending = 0;
}

void graph_taint_read_block(void const* const* block) {
    if (mark_has((u64)(uintptr_t)block)) graph_taint_tick();
}

static u64 result_key(unsigned kind, void const* object) {
    // (KEY_SIMPLIFIED's frame names its intern block, which the hit check looks for)
    return kind == TK_SIMPLIFIED ? (u64)(uintptr_t)object : object_key(kind, object);
}

void graph_taint_end(unsigned kind, void const* object, void const* subject) {
    Checker* checker = current_checker;
    if (!checker || !checker->graph) return;
    mark_add(result_key(kind, object));
    __atomic_fetch_add(&tainted_by_kind[kind < TK_COUNT ? kind : 0], 1, __ATOMIC_RELAXED);
    // (Results kept on a type: is it one an earlier checker made, which the next ones get as it is?)
    bool on_type = kind == TK_MEMBERS || kind == TK_DECLARED_RAW || kind == TK_PARAM_CONSTRAINT;
    if (on_type && subject) {
        u32 chunk = ((Ty const*)subject)->id >> LAZY_TYPE_CHUNK_BITS;
        bool own = false;
        for (u32 i = 0; i < checker->lazy.own_len && !own; i++) own = checker->lazy.own[i] == chunk;
        if (!own) __atomic_fetch_add(&tainted_shared_by_kind[kind], 1, __ATOMIC_RELAXED);
    }
}

// TTTT_GRAPH_WITHHOLD=1 (plan 86 4d's experiment): a Ty-resident result whose computation saw a taint event is left
// not started instead of done, so that the next reader (any worker) computes it again; its computer keeps the value it
// returns. TTTT_GRAPH_WITHHOLD=k,k: only those TK_* kinds. Counted by kind (graph_print_stats())
static u64 withheld_by_kind[TK_COUNT];
bool graph_withhold(unsigned kind, TaintFrame frame) {
    if (graph_taint_now == frame.taint) return false;
    static int on = -1;
    static bool kinds[TK_COUNT];
    if (on < 0) {
        char const* list = getenv("TTTT_GRAPH_WITHHOLD");
        bool any = list && *list && strcmp(list, "0");
        bool all = any && !strcmp(list, "1");
        for (char const* p = list; any && !all && p && *p;) {
            u32 k = (u32)strtoul(p, (char**)&p, 10);
            if (k < TK_COUNT) kinds[k] = true;
            if (*p == ',') p++;
            else break;
        }
        for (u32 k = 0; all && k < TK_COUNT; k++) kinds[k] = true;
        on = any;
    }
    Checker* checker = current_checker;
    if (!on || !checker || !checker->graph || kind >= TK_COUNT || !kinds[kind]) return false;
    __atomic_fetch_add(&withheld_by_kind[kind], 1, __ATOMIC_RELAXED);
    return true;
}

void graph_taint_read(unsigned kind, void const* object) {
    if (mark_has(result_key(kind, object))) graph_taint_tick();
}

#endif

// TTTT_GRAPH_PUBLISH_TAINTED=kind,kind... (KEY_* numbers, node_links' as 256 + kind, 500 the declaration memos; `all`):
// publish those kinds' tainted results all the same (to find which kind's withholding changes the output)
static bool publish_tainted(u32 kind) {
    static int all = -1;
    static bool kinds[512];
    if (all < 0) {
        char const* list = getenv("TTTT_GRAPH_PUBLISH_TAINTED");
        all = list && !strcmp(list, "all");
        for (char const* p = list; p && *p && !all;) {
            u32 k = (u32)strtoul(p, (char**)&p, 10);
            if (k < 512) kinds[k] = true;
            if (*p == ',') p++;
            else break;
        }
    }
    return all || (kind < 512 && kinds[kind]);
}

// Whether a private entry may be published: an identity always, a memo if its computation was clean
static bool entry_tainted(u32 kind, void const* const* block) {
#ifndef TTTT_GRAPH_TAINT
    (void)kind, (void)block, (void)publish_tainted;
    return false;
#else
    if (!graph_taint_marks || publish_tainted(kind)) return false;
    if (mark_has((u64)(uintptr_t)block)) return true;
    switch (kind) {
    case KEY_VARIANCES: case KEY_ALIAS_VARIANCES: return mark_has(object_key(TK_VARIANCES, block[0]));
    default: return false;
    }
#endif
}

static bool link_tainted(u32 kind, void const* subject, void const* value) {
#ifndef TTTT_GRAPH_TAINT
    (void)kind, (void)subject, (void)value;
    return false;
#else
    if (!graph_taint_marks || publish_tainted(256 + kind)) return false;
    switch (kind) {
    case KEY_BASE_CONSTRAINT: return mark_has(object_key(TK_BASE_CONSTRAINT, subject));
    case KEY_FUNCTION_FREE_PARAMS: return mark_has(object_key(TK_FREE_PARAMS, value));
    default: return false;
    }
#endif
}

// Publication

static u64 graph_link_key(u32 kind, void const* subject) {
    return (u64)(kind + 1) << 56 | (u64)(uintptr_t)subject;
}

static u32 block_kind(InternTable const* table, Checker const* checker, InternEntry const* entry, u32* count) {
    u32 n = (u32)(entry->hash >> 48);
    u64 const* key = (u64 const*)(entry->block + 1);
    if (n == 0xffffu) n = (u32)*key++;
    *count = n;
    if (table == &checker->intersection_props) return GRAPH_KIND_INTERSECTION_PROPS;
    return n ? (u32)(key[0] & 255) : 0;
}

static void publish_table(TyGraph* me, Checker* checker, InternTable* private_table, SharedTable* shared) {
    for (u32 i = 0; i < private_table->cap; i++) {
        InternEntry const* entry = &private_table->entries[i];
        if (!entry->hash) continue;
        u32 count;
        u32 kind = block_kind(private_table, checker, entry, &count);
        KindStats* stats = &my stats[kind];
        GraphClass class = graph_kind_class(kind);
        if (class == GRAPH_SCRATCH) {
            __atomic_fetch_add(&stats->scratch, 1, __ATOMIC_RELAXED);
            continue;
        }
        if (!entry->block[0]) {
            __atomic_fetch_add(&stats->unfinished, 1, __ATOMIC_RELAXED);
            continue;
        }
        if (class == GRAPH_MEMO && entry_tainted(kind, entry->block)) {
            __atomic_fetch_add(&stats->tainted, 1, __ATOMIC_RELAXED);
            continue;
        }
        void const** there;
        if (shared_table_insert(shared, entry->hash, entry->block, count, &there)) __atomic_fetch_add(&stats->published, 1, __ATOMIC_RELAXED);
    }
}

typedef struct LinkPublish {
    TyGraph* graph;
} LinkPublish;

static void publish_link(void* arg, u32 kind, void const* subject, void const* value) {
    TyGraph* me = ((LinkPublish*)arg)->graph;
    KindStats* stats = &my stats[256 + (kind & 255)];
    if (graph_kind_class(kind) == GRAPH_SCRATCH) {
        __atomic_fetch_add(&stats->scratch, 1, __ATOMIC_RELAXED);
        return;
    }
    if (!value) {
        __atomic_fetch_add(&stats->unfinished, 1, __ATOMIC_RELAXED);
        return;
    }
    if (graph_kind_class(kind) == GRAPH_MEMO && link_tainted(kind, subject, value)) {
        __atomic_fetch_add(&stats->tainted, 1, __ATOMIC_RELAXED);
        return;
    }
    if (shared_links_insert(my node_links, graph_link_key(kind, subject), value) == value) __atomic_fetch_add(&stats->published, 1, __ATOMIC_RELAXED);
}

// (Pages are made by CAS: a loser frees its own)
static LazyMemo* memo_at(TyGraph* me, u32 slot, bool create) {
    LazyMemo** _Atomic* dir = &my memos[slot >> (LAZY_PAGE_BITS + LAZY_DIR_BITS)];
    LazyMemo** d = atomic_load_explicit(dir, memory_order_acquire);
    if (!d) {
        if (!create) return nullptr;
        LazyMemo** fresh = calloc(1 << LAZY_DIR_BITS, sizeof(LazyMemo*));
        panic_if(!fresh, "out of memory");
        if (atomic_compare_exchange_strong(dir, &d, fresh)) d = fresh;
        else free(fresh);
    }
    LazyMemo* _Atomic* page = (LazyMemo* _Atomic*)&d[(slot >> LAZY_PAGE_BITS) & ((1 << LAZY_DIR_BITS) - 1)];
    LazyMemo* p = atomic_load_explicit(page, memory_order_acquire);
    if (!p) {
        if (!create) return nullptr;
        LazyMemo* fresh = calloc(1 << LAZY_PAGE_BITS, sizeof(LazyMemo));
        panic_if(!fresh, "out of memory");
        if (atomic_compare_exchange_strong(page, &p, fresh)) p = fresh;
        else free(fresh);
    }
    return &p[slot & ((1 << LAZY_PAGE_BITS) - 1)];
}

static double now_ms(void) {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

void graph_publish(TyGraph* me, Checker* checker) {
    double t0 = now_ms();
    publish_table(me, checker, &checker->types, my types);
    double t1 = now_ms();
    publish_table(me, checker, &checker->links, my links);
    double t2 = now_ms();
    publish_table(me, checker, &checker->intersection_props, my intersection_props);
    double t3 = now_ms();
    LinkPublish arg = { me };
    link_table_each(&checker->node_links, publish_link, &arg);
    double t4 = now_ms();
    if (getenv("TTTT_GRAPH_STATS")) eprintln("graph: publish types ", t1 - t0, " links ", t2 - t1, " iprops ", t3 - t2, " node links ", t4 - t3);
    KindStats* stats = &my stats[GRAPH_STAT_MEMOS];
    for (u32 i = 0; i < checker->graph_memo_len; i++) {
        u32 slot = checker->graph_memo_slots[i] & ~(1u << 31);
        if (checker->graph_memo_slots[i] >> 31 && !publish_tainted(GRAPH_STAT_MEMOS)) {
            __atomic_fetch_add(&stats->tainted, 1, __ATOMIC_RELAXED);
            continue;
        }
        publish_declaration_memo(me, checker, slot);
    }
    checker->graph_memo_len = 0;
}

unsigned* graph_variance_lock(Checker* checker) {
    return &checker->graph->variance_lock;
}

// Variance slots (graph.h). The slots are the graph's arena's: the first checker's, never recycled with the flag
VarianceSlot* graph_variance_slot(Checker* checker, void const* subject, bool alias, bool create) {
    TyGraph* me = checker->graph;
    if (!me || !my concurrent) return nullptr;
    u64 key = (u64)alias << 56 | (u64)(uintptr_t)subject;
    VarianceSlot* ret = (VarianceSlot*)shared_links_get(my variance_slots, key);
    if (ret || !create) return ret;
    VarianceSlot* fresh = arena_new(&checker->arena, VarianceSlot);
    *fresh = (VarianceSlot){ .subject = subject, .alias = alias };
    return (VarianceSlot*)shared_links_insert(my variance_slots, key, fresh);
}

void graph_variance_claimed(Checker* checker, VarianceSlot* slot) {
    if (checker->graph_variance_claims_len == checker->graph_variance_claims_cap) {
        checker->graph_variance_claims_cap = checker->graph_variance_claims_cap ? checker->graph_variance_claims_cap * 2 : 16;
        checker->graph_variance_claims = realloc(checker->graph_variance_claims, sizeof(VarianceSlot*) * checker->graph_variance_claims_cap);
        panic_if(!checker->graph_variance_claims, "out of memory");
    }
    checker->graph_variance_claims[checker->graph_variance_claims_len++] = slot;
}

void graph_variances_publish(Checker* checker, void const* (*value_of)(Checker* checker, VarianceSlot const* slot)) {
    for (u32 i = 0; i < checker->graph_variance_claims_len; i++) {
        VarianceSlot* slot = checker->graph_variance_claims[i];
        void const* value = value_of(checker, slot);
        if (value) {
            __atomic_store_n(&slot->value, value, __ATOMIC_RELAXED);
            LAZY_DONE(slot->state);
        } else {
            __atomic_store_n(&slot->state, (unsigned)LAZY_STATE_NOT_STARTED, __ATOMIC_RELEASE);
        }
    }
    checker->graph_variance_claims_len = 0;
}

static void publish_declaration_memo(TyGraph* me, Checker* checker, u32 slot) {
    KindStats* stats = &my stats[GRAPH_STAT_MEMOS];
    LazyType lazy = { .tag = LAZY_TYPE_EVALUATED, .slot = slot };
    LazySlots* saved = current_lazy_slots;
    current_lazy_slots = &checker->lazy;
    LazyMemo const* memo = lazy_type_state_peek(&lazy);
    current_lazy_slots = saved;
    if (!memo || !memo->done) {
        __atomic_fetch_add(&stats->unfinished, 1, __ATOMIC_RELAXED);
        return;
    }
    LazyMemo* shared = memo_at(me, slot, true);
    // (Written done last, read done first: a reader sees a whole memo or none)
    pthread_mutex_lock(&my memo_lock);
    if (!shared->done) {
        LazyMemo copy = *memo;
        copy.hits = 0;
        copy.done = false;
        *shared = copy;
        atomic_thread_fence(memory_order_release);
        shared->done = true;
        __atomic_fetch_add(&stats->published, 1, __ATOMIC_RELAXED);
    }
    pthread_mutex_unlock(&my memo_lock);
}

void const* graph_link_get(TyGraph const* me, u32 kind, void const* subject) {
    void const* ret = shared_links_get(my node_links, graph_link_key(kind, subject));
    if (ret) __atomic_fetch_add(&((TyGraph*)me)->stats[256 + (kind & 255)].shared_hits, 1, __ATOMIC_RELAXED);
    return ret;
}

void graph_count_lost(TyGraph* me, u32 kind) {
    __atomic_fetch_add(&my stats[kind < GRAPH_STAT_KINDS ? kind : 0].lost, 1, __ATOMIC_RELAXED);
}

void const* graph_link_insert(TyGraph* me, u32 kind, void const* subject, void const* value) {
    void const* there = shared_links_insert(my node_links, graph_link_key(kind, subject), value);
    if (there == value) __atomic_fetch_add(&my stats[256 + (kind & 255)].published, 1, __ATOMIC_RELAXED);
    return there;
}

// Lazy fields in flight on another worker (ty.h's LAZY_AWAIT(), plan 86 4d)

static unsigned lazy_field_load(void* field, size_t size) {
    return size == 1 ? __atomic_load_n((uint8_t*)field, __ATOMIC_ACQUIRE) : __atomic_load_n((unsigned*)field, __ATOMIC_ACQUIRE);
}

static void lazy_field_reset(void* field, size_t size, unsigned seen) {
    if (size == 1) {
        uint8_t expected = (uint8_t)seen;
        __atomic_compare_exchange_n((uint8_t*)field, &expected, (uint8_t)LAZY_STATE_NOT_STARTED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    } else {
        unsigned expected = seen;
        __atomic_compare_exchange_n((unsigned*)field, &expected, (unsigned)LAZY_STATE_NOT_STARTED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
}

bool lazy_claim_slow(Checker* me, void* field, size_t size) {
    unsigned mine = LAZY_STATE_IN_FLIGHT | (unsigned)me->graph_worker << 2;
    unsigned seen = lazy_field_load(field, size);
    if (seen == mine) return true;
    if (seen != LAZY_STATE_NOT_STARTED) return false;
    if (size == 1) {
        uint8_t expected = LAZY_STATE_NOT_STARTED;
        return __atomic_compare_exchange_n((uint8_t*)field, &expected, (uint8_t)mine, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    unsigned expected = LAZY_STATE_NOT_STARTED;
    return __atomic_compare_exchange_n((unsigned*)field, &expected, mine, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

// Whether `me` waiting for `owner` closes a cycle of waits, and if so, whether `me` has the highest number in it: the
// one that breaks it, whichever saw it first (under wait_lock)
// Which one yields: the worker whose file is latest in program order (Checker.file_order), ties by the higher number.
// K1 enters whichever of two mutually dependent results its file order reaches first and makes the other the inner,
// circular one: the yielder is the worker K1 would have reached later. (TTTT_GRAPH_YIELD=worker: by number only)
static u64 yield_rank(TyGraph const* me, unsigned worker) {
    static int by_worker = -1;
    if (by_worker < 0) by_worker = getenv("TTTT_GRAPH_YIELD") && !strcmp(getenv("TTTT_GRAPH_YIELD"), "worker");
    Checker const* c = my workers[worker];
    u64 order = c && !by_worker ? __atomic_load_n(&c->file_order, __ATOMIC_RELAXED) : 0;
    return order << 8 | worker;
}

static bool breaks_wait_cycle(TyGraph const* me, unsigned waiter, unsigned owner) {
    unsigned highest = waiter;
    u64 highest_rank = yield_rank(me, waiter);
    for (unsigned at = owner, n = 0; at && n < 64; at = my waiting_on[at], n++) {
        if (at == waiter) return highest == waiter;
        u64 rank = yield_rank(me, at);
        if (rank > highest_rank) highest = at, highest_rank = rank;
    }
    return false;
}

static u64 now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)t.tv_sec * 1000000000ull + (u64)t.tv_nsec;
}

// Waits until the field isn't the same worker's in flight any more: true (look at it again). False: waiting would close
// a cycle of waits this worker is the one to break; it goes on as if the field were its own in flight. A short spin,
// then sleeps growing to a millisecond: a worker blocked behind a long computation doesn't burn a core
// (A breakpoint for debuggers: where a wait cycle is broken, scripts in plan 86's 4d status)
[[gnu::noinline]] void graph_fallback_hook(Checker* me, void* field) {
    __asm__ volatile("" :: "r"(me), "r"(field) : "memory");
}

void graph_fallback_hook(Checker* me, void* field);
bool lazy_wait_slow(Checker* me, void* field, size_t size) {
    TyGraph* graph = me->graph;
    unsigned seen = lazy_field_load(field, size);
    unsigned owner = seen >> 2;
    // (A retired worker won't finish it: not started again, for whoever claims it next)
    if (LAZY_IN_FLIGHT(seen) && owner && owner < 64 && atomic_load(&graph->retired[owner])) {
        lazy_field_reset(field, size, seen);
        return true;
    }
    if (LAZY_IN_FLIGHT(seen) && !owner && getenv("TTTT_GRAPH_LOUD_FALLBACKS")) {
        static _Atomic u32 told;
        if (atomic_fetch_add(&told, 1) < 5) eprintln("graph: worker ", (u32)me->graph_worker, " meets a field in flight without an owner");
    }
    if (!LAZY_IN_FLIGHT(seen) || !owner || owner == me->graph_worker) return true;
    u64 start = now_ns();
    bool ret = true;
    pthread_mutex_lock(&graph->wait_lock);
    graph->waiting_on[me->graph_worker] = (uint8_t)owner;
    pthread_mutex_unlock(&graph->wait_lock);
    u64 sleep_ns = 2000;
    for (u32 round = 0;; round++) {
        if (lazy_field_load(field, size) != seen) break;
        if (atomic_load(&graph->retired[owner])) {
            lazy_field_reset(field, size, seen);
            break;
        }
        if (round < 64) {
            __builtin_ia32_pause();
            continue;
        }
        pthread_mutex_lock(&graph->wait_lock);
        bool breaks = breaks_wait_cycle(graph, me->graph_worker, owner);
        pthread_mutex_unlock(&graph->wait_lock);
        if (breaks) {
            if (getenv("TTTT_GRAPH_LOUD_FALLBACKS")) {
                void* ra = __builtin_return_address(0);
                eprintln("graph: worker ", (u32)me->graph_worker, " breaks a wait cycle on ", (u32)owner, " at ",
                    (u64)((uintptr_t)ra - (uintptr_t)&graph_print_stats));
            }
            graph_fallback_hook(me, field);
            me->graph_fallbacks++;
            // A redo instead (plan 86 4d): the worker gives its file up, its claims become nobody's, and the file is
            // checked again by a fresh checker once the other side has finished (check_share())
            if (me->redo_jmp) {
                pthread_mutex_lock(&graph->wait_lock);
                graph->waiting_on[me->graph_worker] = 0;
                pthread_mutex_unlock(&graph->wait_lock);
                longjmp(*(jmp_buf*)me->redo_jmp, 1);
            }
            ret = false;
            break;
        }
        // (Then yields: an owner preempted on a loaded machine gets the core back, and a waiter that slept instead fell in
        // behind it, waiting again at its next field: drizzle K12 on 12 threads with every memo published, 89k waits and
        // 1.59 s mean wall sleeping at once, 660 and 0.85 s yielding 50 times first. TTTT_GRAPH_WAIT_YIELDS=N)
        static int yields = -1;
        if (yields < 0) yields = getenv("TTTT_GRAPH_WAIT_YIELDS") ? atoi(getenv("TTTT_GRAPH_WAIT_YIELDS")) : 50;
        if (round < 64 + (u32)yields) {
            sched_yield();
            continue;
        }
        struct timespec pause = { 0, (long)sleep_ns };
        nanosleep(&pause, nullptr);
        if (sleep_ns < 1000000) sleep_ns *= 2;
    }
    pthread_mutex_lock(&graph->wait_lock);
    graph->waiting_on[me->graph_worker] = 0;
    pthread_mutex_unlock(&graph->wait_lock);
    me->graph_waits++;
    me->graph_wait_ns += now_ns() - start;
    return ret;
}

bool graph_memo_take(TyGraph* me, u32 slot, LazyMemo* memo) {
    LazyMemo const* shared = memo_at(me, slot, false);
    if (!shared || !shared->done) return false;
    atomic_thread_fence(memory_order_acquire);
    *memo = *shared;
    __atomic_fetch_add(&my stats[GRAPH_STAT_MEMOS].shared_hits, 1, __ATOMIC_RELAXED);
    return true;
}

// Memo publication at the computation's end (graph.h)

bool graph_memo_eager(void) {
    static int on = -1;
    if (__builtin_expect(on < 0, 0)) on = !(getenv("TTTT_GRAPH_MEMO_EAGER") && !strcmp(getenv("TTTT_GRAPH_MEMO_EAGER"), "0"));
    return on;
}

// (Only once there is someone to read them: a graph of one checker, `--checkers 1`, publishes nothing)
bool graph_memo_sharing(Checker const* checker) {
    return checker->graph_memo_on;
}

// (Debug: TTTT_GRAPH_MEMO_LATE=k,k,...: those memo kinds (KEY_* numbers, 254 intersection props) aren't published
// before the share's end: a bisection of what eager publication changes)
bool graph_memo_late(u32 kind) {
    static int init = -1;
    static bool kinds[256];
    if (__builtin_expect(init < 0, 0)) {
        char const* env = getenv("TTTT_GRAPH_MEMO_LATE");
        for (char const* p = env; p && *p;) {
            char* end;
            unsigned long k = strtoul(p, &end, 10);
            if (end == p) break;
            if (k < 256) kinds[k] = true;
            p = *end == ',' ? end + 1 : end;
        }
        init = env != nullptr;
    }
    return init && kind < 256 && kinds[kind];
}

// (TTTT_DEBUG_MEMO_PUBLISH=1: every published entry's value, checked when the program ends: a memo written again after
// it was published changes what the others read under them)
typedef struct PublishedMemo {
    void const** block;
    void const* value;
} PublishedMemo;
static PublishedMemo* debug_published;
static u64 debug_published_len, debug_published_cap;
static pthread_mutex_t debug_published_lock = PTHREAD_MUTEX_INITIALIZER;
static int debug_memo_publish = -1;

static void debug_memo_published(void const** block) {
    pthread_mutex_lock(&debug_published_lock);
    if (debug_published_len == debug_published_cap) {
        debug_published_cap = debug_published_cap ? debug_published_cap * 2 : 1 << 16;
        debug_published = realloc(debug_published, sizeof(PublishedMemo) * debug_published_cap);
        panic_if(!debug_published, "out of memory");
    }
    debug_published[debug_published_len++] = (PublishedMemo){ block, block[0] };
    pthread_mutex_unlock(&debug_published_lock);
}

static void debug_memo_check(void) {
    u64 changed[256] = {0}, total = 0;
    for (u64 i = 0; i < debug_published_len; i++) {
        PublishedMemo const* p = &debug_published[i];
        if (p->block[0] == p->value) continue;
        u64 hash = ((u64 const*)p->block)[-1];
        u32 count = (u32)(hash >> 48);
        u64 const* key = (u64 const*)(p->block + 1);
        if (count == 0xffffu) count = (u32)*key++;
        changed[count ? key[0] & 255 : 0]++;
        total++;
    }
    eprintln("graph: ", total, " of ", debug_published_len, " published memo entries written again after");
    for (u32 k = 0; k < 256; k++) if (changed[k]) eprintln("graph:   kind ", k, ": ", changed[k]);
}

typedef struct GraphMemoLogEntry {
    uintptr_t entry;
    u32 limit_hits;
} GraphMemoLogEntry;

static SharedTable* memo_log_table(TyGraph* me, uintptr_t tag) {
    return tag == 0 ? my types : tag == 1 ? my links : my intersection_props;
}

void graph_memo_log_put(Checker* checker, InternTable const* table, void const** block, u32 kind) {
    uintptr_t tag;
    if (table == &checker->types) tag = 0;
    else if (table == &checker->links) tag = 1;
    else tag = 2;
    if (tag != 2 && graph_kind_class(kind) != GRAPH_MEMO) return;
    if (!graph_memo_sharing(checker) || graph_memo_late(tag == 2 ? GRAPH_KIND_INTERSECTION_PROPS : kind)) return;
    // (Variances on threads go through their slots, graph_variance_slot(): published entry by entry, two workers measured
    // one type at once, drizzle K12's View [0 0 0] next to K1's [1 1 1], and whichever was read first spread (TS2344 in
    // 2-4 runs of 20); kept private instead, every worker measured them again in a warm graph (excalidraw's TS2345s in
    // 20 of 20). One checker after another they go out as a group when the outermost measurement ends,
    // graph_memo_flush())
    if ((kind == KEY_VARIANCES || kind == KEY_ALIAS_VARIANCES) && tag == 1 && checker->graph->concurrent) return;
    if (!checker->graph_memo_log_limit) checker->graph_memo_log_limit = 64;
    // (Before the new one goes in, whose block isn't filled yet. Entries still without a value stay: the limit grows
    // with them, a deep computation doesn't flush every put)
    if (checker->graph_memo_log_len >= checker->graph_memo_log_limit) graph_memo_flush(checker);
    if (checker->graph_memo_log_len == checker->graph_memo_log_cap) {
        checker->graph_memo_log_cap = checker->graph_memo_log_cap ? checker->graph_memo_log_cap * 2 : 256;
        checker->graph_memo_log = realloc(checker->graph_memo_log, sizeof(GraphMemoLogEntry) * checker->graph_memo_log_cap);
        panic_if(!checker->graph_memo_log, "out of memory");
    }
    checker->graph_memo_log[checker->graph_memo_log_len++] = (GraphMemoLogEntry){ (uintptr_t)block | tag, checker->instantiation_limit_hits };
}

void graph_memo_log_unput(Checker* checker, void const** block) {
    u32 n = checker->graph_memo_log_len;
    if (n && (checker->graph_memo_log[n - 1].entry & ~(uintptr_t)3) == (uintptr_t)block) checker->graph_memo_log_len--;
}

void graph_memo_flush(Checker* checker) {
    TyGraph* me = checker->graph;
    if (!me) return;
    if (__builtin_expect(debug_memo_publish < 0, 0)) debug_memo_publish = getenv("TTTT_DEBUG_MEMO_PUBLISH") != nullptr;
    u32 kept = 0;
    for (u32 i = 0; i < checker->graph_memo_log_len; i++) {
        GraphMemoLogEntry logged = checker->graph_memo_log[i];
        uintptr_t entry = logged.entry;
        void const** block = (void const**)(entry & ~(uintptr_t)3);
        if (!block[0]) {
            checker->graph_memo_log[kept++] = logged;
            continue;
        }
        // (The instantiation limits ran out while it may have been computed (TS2589): what it is was cut short in this
        // worker's history, which the others don't share. It stays the worker's own)
        if (logged.limit_hits != checker->instantiation_limit_hits) {
            __atomic_fetch_add(&my stats[GRAPH_STAT_KINDS - 1].tainted, 1, __ATOMIC_RELAXED);
            continue;
        }
        u64 hash = ((u64 const*)block)[-1];
        u32 count = (u32)(hash >> 48);
        u64 const* key = (u64 const*)(block + 1);
        if (count == 0xffffu) count = (u32)*key++;
        u32 kind = (entry & 3) == 2 ? GRAPH_KIND_INTERSECTION_PROPS : count ? (u32)(key[0] & 255) : 0;
        // (Variances go out as a group when the outermost measurement ends: what a cycle measures depends on where it is
        // entered, and one worker's entry mustn't be mixed with another's, plan 86 4d)
        if ((kind == KEY_VARIANCES || kind == KEY_ALIAS_VARIANCES) && checker->variance_depth) {
            checker->graph_memo_log[kept++] = logged;
            continue;
        }
        KindStats* stats = &my stats[kind];
        if (entry_tainted(kind, block)) {
            __atomic_fetch_add(&stats->tainted, 1, __ATOMIC_RELAXED);
            continue;
        }
        void const** there;
        if (shared_table_insert(memo_log_table(me, entry & 3), hash, block, count, &there)) {
            __atomic_fetch_add(&stats->published, 1, __ATOMIC_RELAXED);
            if (debug_memo_publish) debug_memo_published(block);
        } else if (there != block) {
            // (Another checker's came first: this one reads that from now on)
            block[0] = __atomic_load_n(there, __ATOMIC_ACQUIRE);
            __atomic_fetch_add(&stats->lost, 1, __ATOMIC_RELAXED);
        }
    }
    checker->graph_memo_log_len = kept;
    u32 limit = checker->graph_memo_log_limit ? checker->graph_memo_log_limit : 64;
    while (kept * 2 > limit) limit *= 2;
    while (limit > 64 && kept * 8 < limit) limit /= 2;
    checker->graph_memo_log_limit = limit;
}


void graph_memo_done(Checker* checker, u32 slot, bool tainted) {
    static int late = -1; // (Debug: TTTT_GRAPH_MEMO_EAGER=nodecl, declaration memos at the share's end)
    if (late < 0) late = getenv("TTTT_GRAPH_MEMO_EAGER") && !strcmp(getenv("TTTT_GRAPH_MEMO_EAGER"), "nodecl");
    if (graph_memo_eager() && !late) {
        if (!graph_memo_sharing(checker)) return;
        if (tainted && !publish_tainted(GRAPH_STAT_MEMOS)) {
            __atomic_fetch_add(&checker->graph->stats[GRAPH_STAT_MEMOS].tainted, 1, __ATOMIC_RELAXED);
            return;
        }
        publish_declaration_memo(checker->graph, checker, slot);
        return;
    }
    if (checker->graph_memo_len == checker->graph_memo_cap) {
        checker->graph_memo_cap = checker->graph_memo_cap ? checker->graph_memo_cap * 2 : 1024;
        checker->graph_memo_slots = realloc(checker->graph_memo_slots, sizeof(u32) * checker->graph_memo_cap);
        panic_if(!checker->graph_memo_slots, "out of memory");
    }
    checker->graph_memo_slots[checker->graph_memo_len++] = slot | (u32)tainted << 31;
}

static char const* kind_name(u32 kind) {
    switch (kind) {
#define K(x) case x: return #x;
    K(KEY_LITERAL) K(KEY_UNION) K(KEY_INTERSECTION) K(KEY_REFERENCE) K(KEY_TUPLE_TARGET) K(KEY_KEYOF) K(KEY_INDEXED)
    K(KEY_CONDITIONAL) K(KEY_SUBSTITUTION) K(KEY_ANONYMOUS_INSTANCE) K(KEY_UNIQUE_SYMBOL) K(KEY_FRESH_LITERAL)
    K(KEY_REGULAR_UNION) K(KEY_WIDENED_OBJECTS) K(KEY_REGULAR_OBJECT_LITERAL) K(KEY_TEMPLATE_LITERAL)
    K(KEY_STRING_MAPPING) K(KEY_MAPPED_KEY_PARAM) K(KEY_INSTANTIATION_QUERY) K(KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT)
    K(KEY_CONDITIONAL_INSTANCE) K(KEY_TYPE_PARAM) K(KEY_SYMBOL_LINKS) K(KEY_GENERIC_PARAMS) K(KEY_ALIAS_INSTANCE)
    K(KEY_NODE) K(KEY_NAMESPACE) K(KEY_TYPE_ONLY_EXPORT) K(KEY_MERGED_VALUE) K(KEY_DEFERRED_OUTER)
    K(KEY_CALL_RESOLUTION) K(KEY_DEFERRED_REFERENCE) K(KEY_STRUCTURAL) K(KEY_KEYOF_PROPERTIES) K(KEY_SIMPLIFIED)
    K(KEY_NORMALIZED_INTERSECTION) K(KEY_MAPPED_FILTERING) K(KEY_PROP_MAPPED_TYPE) K(KEY_PROP_WRITE_TYPE)
    K(KEY_UNION_ACCESS) K(KEY_BASE_CONSTRAINT) K(KEY_INFERRED_TRUE_BRANCH) K(KEY_DISTRIBUTION_DEPENDENT)
    K(KEY_DISTRIBUTIVE_CONSTRAINT) K(KEY_IMPLIED_CONSTRAINT) K(KEY_RESTRICTIVE_PARAM) K(KEY_TYPE_VARIABLE_REDUCTION)
    K(KEY_DEFINITELY_RELATED) K(KEY_CANONICAL_SIGNATURE) K(KEY_BASE_SIGNATURE) K(KEY_VARIANCES) K(KEY_MARKER)
    K(KEY_ALIAS_VARIANCES) K(KEY_ALIAS_MARKER) K(KEY_CHECK_MARKER) K(KEY_CHECK_MARKER_REF) K(KEY_GENERIC_RELATION)
    K(KEY_DECL_GROUP) K(KEY_DECL_GROUP_OF) K(KEY_FUNCTION) K(KEY_ITERATION_TYPES) K(KEY_UNIQUE_SYMBOL_NAME)
    K(KEY_PRIVATE_NAME) K(KEY_PRIVATE_NAME_CLASS) K(KEY_WIDENED_NULLABLE) K(KEY_INTERSECTION_PROP_CONTEXT)
    K(KEY_UNDEFINED_PROPERTY) K(KEY_SYNTHETIC_PROP_PARTS) K(KEY_FUNCTION_RETURN) K(KEY_UNION_PAIR)
    K(KEY_INTERSECTION_OF) K(KEY_REVERSE_MAPPED) K(KEY_UNION_KEY_NAME) K(KEY_UNION_KEY_MEMBER) K(KEY_FLOW_NOT_UNIT)
    K(KEY_EVOLVING_ARRAY) K(KEY_REDUCED_UNION) K(KEY_SIGNATURE_INSTANCE) K(KEY_SIGNATURE_TYPE)
    K(KEY_FUNCTION_FREE_PARAMS) K(KEY_FUNCTION_TYPE_MENTIONS) K(KEY_ARRAY_THIS_PROPS) K(KEY_TUPLE_NAME_ORDER)
    K(KEY_INDEX_NAME_ORDER) K(KEY_DECIMAL_ORDER) K(KEY_UNION_NODE) K(KEY_NAMESPACE_IMPORT) K(KEY_MODULE_CLONE)
    K(KEY_THISLESS_SIGNATURES) K(KEY_CLASS_MEMBER_SYMBOLS) K(KEY_IMPLEMENTATION_SIGNATURE)
#undef K
    case GRAPH_KIND_INTERSECTION_PROPS: return "intersection_props";
    default: return nullptr;
    }
}

// TTTT_GRAPH_STATS=1 (at the end of the typecheck): by kind, what was published, kept private and found shared
void graph_print_stats(TyGraph const* me) {
    if (debug_memo_publish > 0) debug_memo_check();
    eprintln("graph: ", (u32)atomic_load(&((TyGraph*)me)->redos), " files checked again after a broken wait cycle");
    eprintln("graph: ", my checkers, " checkers, ", shared_table_len(my types), " types entries, ", shared_table_len(my links),
        " links, ", shared_table_len(my intersection_props), " intersection props, ", shared_links_len(my node_links),
        " node links shared");
    for (u32 w = 1; w <= my checkers && w < 64; w++) {
        Checker const* c = my workers[w];
        if (c && c->instantiation_limit_hits) eprintln("graph: worker ", w, " instantiation limit hits ", c->instantiation_limit_hits);
        if (c && (c->graph_waits || c->graph_fallbacks)) {
            eprintln("graph: worker ", w, " waited ", c->graph_waits, " times, ", (double)c->graph_wait_ns / 1e6, " ms, ",
                c->graph_fallbacks, " wait cycles broken (fallbacks)");
        }
    }
    eprintln("graph: kind  shared-hits  published  tainted  scratch  unfinished");
    KindStats total = {0};
    for (u32 k = 0; k < GRAPH_STAT_KINDS; k++) {
        KindStats const* s = &my stats[k];
        if (!s->shared_hits && !s->published && !s->tainted && !s->scratch && !s->unfinished) continue;
        char const* name = k == GRAPH_STAT_MEMOS ? "declaration memos" : kind_name(k & 255);
        char other[32];
        if (!name) {
            snprintf(other, sizeof other, "kind %u", k & 255);
            name = other;
        }
        eprintln("graph: ", k >= 256 && k != GRAPH_STAT_MEMOS ? "N " : "  ", name, " ", s->shared_hits, " ", s->published,
            " ", s->tainted, " ", s->scratch, " ", s->unfinished);
        total.shared_hits += s->shared_hits;
        total.published += s->published;
        total.tainted += s->tainted;
        total.scratch += s->scratch;
        total.unfinished += s->unfinished;
    }
    eprintln("graph: total ", total.shared_hits, " ", total.published, " ", total.tainted, " ", total.scratch, " ",
        total.unfinished);
#ifdef TTTT_GRAPH_TAINT
    eprintln("graph: taint events ", (i64)graph_taint_now, ", sweeps ", taint_events_seen, ", marked ", (i64)marks.len);
    static char const* const tk_names[] = { "members", "declared raw", "prop type", "signature return", "param constraint",
        "symbol declared", "function return", "base constraint", "simplified", "variances", "free params" };
    for (u32 k = 0; k < TK_COUNT; k++) {
        if (!tainted_by_kind[k]) continue;
        char const* name = k >= TK_MEMBERS && k <= TK_FREE_PARAMS ? tk_names[k - TK_MEMBERS] :
            k >= TK_LAZY_DECL && k < TK_LAZY_EXPR ? "declaration memo" : kind_name(k);
        eprintln("graph: tainted ", name ? name : "?", " ", tainted_by_kind[k], " (on an earlier checker's type: ",
            tainted_shared_by_kind[k], "), withheld ", withheld_by_kind[k]);
    }
#else
    eprintln("graph: taint not compiled in (-DTTTT_GRAPH_TAINT): memo kinds published whatever their computation saw");
#endif
}
