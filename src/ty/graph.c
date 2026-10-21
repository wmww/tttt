// --shared-graph: the type graph checkers share, see graph.h and APPROACH.md
#include "ty/graph.h"
#include "ty/ty_internal.h"
#include "lazy_type.h"
#include "program.h"
#include "ty/shared_table.h"
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

// TTTT_GRAPH_STATS' counters: global, so every worker's increment of one would take the same cache line exclusively
// (several million a run, from every core). Counted only when the stats are printed
static bool graph_stats_on;
#define GRAPH_STAT_ADD(p, n, order) do { if (graph_stats_on) __atomic_fetch_add((p), (n), __ATOMIC_RELAXED); } while (0)


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
    pthread_mutex_t adopt_lock; // graph_adopt() on a worker thread (a shadowed worker's fresh checker)
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
    SharedLinks* relations[TY_RELATION_COUNT]; // graph_relation_get()
    _Atomic u64 relation_hits, relation_puts;
    // Declaration memos by slot, like LazySlots.top (pages made by CAS): each a shared word to an immutable copy of the
    // memo that finished cleanly first (plan 86 stage 4f)
    SharedWord* _Atomic* _Atomic memos[1 << LAZY_TOP_BITS];
    KindStats stats[GRAPH_STAT_KINDS];
    u32 checkers;
    Checker* workers[64]; // By Checker.graph_worker
};


static GraphClass graph_kind_class_of(u32 kind);
static char const* kind_name(u32 kind);
// (Debug: TTTT_GRAPH_NOSHARE=k,k,... or =all-but:k,k: kinds kept private, a bisection of what sharing breaks)
// (A table: asked at every memo kind's miss and put)
static uint8_t kind_classes[512];
uint8_t graph_memo_tracked_kinds[256];
static bool memo_identity_valued(u32 kind);
static int kind_classes_init;
[[gnu::noinline, gnu::cold]] static void kind_classes_make(void) {
    static uint8_t noshare_kinds[256];
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
    for (u32 kind = 0; kind < c_arraylen(kind_classes); kind++) {
        GraphClass c = graph_kind_class_of(kind);
        kind_classes[kind] = (uint8_t)(c != GRAPH_SCRATCH && kind < 256 && noshare_kinds[kind] ? GRAPH_SCRATCH : c);
    }
    for (u32 kind = 0; kind < 256; kind++) graph_memo_tracked_kinds[kind] = kind_classes[kind] == GRAPH_MEMO && !memo_identity_valued(kind);
    __atomic_store_n(&kind_classes_init, 1, __ATOMIC_RELEASE);
}

GraphClass graph_kind_class(u32 kind) {
    if (__builtin_expect(!__atomic_load_n(&kind_classes_init, __ATOMIC_ACQUIRE), 0)) kind_classes_make();
    return kind < c_arraylen(kind_classes) ? (GraphClass)kind_classes[kind] : graph_kind_class_of(kind);
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
    case KEY_CANONICAL_SIGNATURE: case KEY_BASE_SIGNATURE: case KEY_VARIANCES:
    case KEY_CHECK_MARKER_REF: case KEY_ITERATION_TYPES: case KEY_WIDENED_NULLABLE:
    case KEY_INTERSECTION_PROP_CONTEXT: case KEY_UNION_PAIR:
    case KEY_INTERSECTION_OF: case KEY_REVERSE_MAPPED: case KEY_UNION_KEY_NAME: case KEY_UNION_KEY_MEMBER: case KEY_FLOW_NOT_UNIT:
    case KEY_EVOLVING_ARRAY: case KEY_REDUCED_UNION: case KEY_FUNCTION_FREE_PARAMS: case KEY_FUNCTION_TYPE_MENTIONS:
    case KEY_ARRAY_THIS_PROPS: case KEY_TUPLE_NAME_ORDER: case KEY_INDEX_NAME_ORDER: case KEY_DECIMAL_ORDER:
    case KEY_THISLESS_SIGNATURES: case GRAPH_KIND_INTERSECTION_PROPS:
        return GRAPH_MEMO;
    // KEY_ALIAS_MARKER says which alias instances this checker's own variance measurements made (plan 86 4f: with
    // variance groups poisoned and measured privately, a shared flag told a checker that never measured the alias that
    // an instance it met was a marker, react-navigation's spurious TS2322s at K16 T1);
    // KEY_GENERIC_RELATION numbers relations per checker; KEY_UNDEFINED_PROPERTY is each checker's link from a name to
    // its program-wide entry (members.c's undefined_choice(), the same for every checker); the rest is per expression,
    // per call, flow, inference, report-once
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
static void publish_identities(TyGraph* me, Checker* checker);

// (Its tables look in the graph's from now on: another checker reads the graph)
static void graph_share_tables(TyGraph* me, Checker* checker) {
    checker->types.others = my types;
    checker->links.others = my links;
    checker->intersection_props.others = my intersection_props;
}

TyGraph* graph_new(Checker* first) {
    graph_kind_class(0); // (Its tables, before any worker asks)
    graph_stats_on = getenv("TTTT_GRAPH_STATS") != nullptr;
    TyGraph* me = calloc(1, sizeof(TyGraph));
    panic_if(!me, "out of memory");
    my types = shared_table_new();
    my links = shared_table_new();
    my intersection_props = shared_table_new();
    my node_links = shared_links_new();
    my variance_slots = shared_links_new();
    for (u32 i = 0; i < TY_RELATION_COUNT; i++) my relations[i] = shared_links_new();
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
    // (Atomic: graph_can_retire() reads it without the lock)
    __atomic_store_n(&my checkers, my checkers + 1, __ATOMIC_RELAXED);
    panic_if(my checkers > 63, "--shared-graph: at most 63 checkers");
    checker->graph_worker = (uint8_t)my checkers;
    // (Memo publication from the second checker on: the first one's, made alone, went with graph_new()'s publication)
    {
        checker->graph_memo_on = true;
        graph_share_tables(me, checker);
        // (Not written again once on: a shadowed worker's fresh checker adopts while the first one runs, which reads it. Alone it
        // published no identity: it does now, before the newcomer looks. It isn't running: a graph of one checker gets
        // its second one at the start, a recycle's handover or a recheck)
        Checker* first = my workers[1];
        if (first && !first->graph_memo_on) {
            publish_identities(me, first);
            graph_share_tables(me, first);
            first->graph_memo_on = true;
        }
    }
    __atomic_store_n(&my workers[my checkers], checker, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&my adopt_lock);
}

bool graph_can_retire(TyGraph const* me) {
    // (Relaxed: every worker asks, the first ones at once)
    return my concurrent && __atomic_load_n(&((TyGraph*)me)->checkers, __ATOMIC_RELAXED) < 60;
}

void graph_set_concurrent(TyGraph* me, bool concurrent) {
    my concurrent = concurrent;
}

// (Nothing is handed over any more: atoms and ids are the program's (4c), the singletons the graph's (TySingletons),
// and a checker's id table is told of every chunk (lazy_slots_share()))
void graph_enter(TyGraph* me, Checker* checker) {
    (void)me, (void)checker;
}

static void memo_taint_drop(Checker* checker);
void graph_release(TyGraph* me, Checker* checker) {
    if (checker->graph_worker < 64 && my workers[checker->graph_worker] == checker) my workers[checker->graph_worker] = nullptr;
    // (The graph's: freed by graph_drop())
    free(checker->graph_memo_log);
    memo_taint_drop(checker);
    free(checker->graph_variance_claims);
    checker->graph_variance_claims = nullptr;
    checker->graph_variance_claims_len = checker->graph_variance_claims_cap = 0;
    checker->graph_memo_log = nullptr;
    checker->graph_memo_log_len = checker->graph_memo_log_cap = 0;
}

void graph_drop(TyGraph* me) {
    if (!me) return;
    shared_table_drop(my types);
    shared_table_drop(my links);
    shared_table_drop(my intersection_props);
    shared_links_drop(my node_links);
    shared_links_drop(my variance_slots);
    for (u32 i = 0; i < TY_RELATION_COUNT; i++) shared_links_drop(my relations[i]);
    for (size_t i = 0; i < c_arraylen(my memos); i++) {
        if (!my memos[i]) continue;
        for (size_t j = 0; j < (1 << LAZY_DIR_BITS); j++) free(my memos[i][j]);
        free((void*)my memos[i]);
    }
    free(me);
}

// Statistics

void graph_count_shared_hit(InternTable const* table, u64 const* words, u32 count) {
    Checker* checker = current_checker;
    if (!checker || !checker->graph || !count) return;
    u32 kind = table == &checker->intersection_props ? GRAPH_KIND_INTERSECTION_PROPS : (u32)(words[0] & 255);
    GRAPH_STAT_ADD(&checker->graph->stats[kind].shared_hits, 1, __ATOMIC_RELAXED);
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
            GRAPH_STAT_ADD(&stats->scratch, 1, __ATOMIC_RELAXED);
            continue;
        }
        if (!entry->block[0]) {
            GRAPH_STAT_ADD(&stats->unfinished, 1, __ATOMIC_RELAXED);
            continue;
        }
        void const** there;
        if (shared_table_insert(shared, entry->hash, entry->block, count, &there)) GRAPH_STAT_ADD(&stats->published, 1, __ATOMIC_RELAXED);
    }
}

// The identities a checker made alone on the graph (InternTable.others null: intern_publish() and ty_link_publish()
// kept them private), published as intern_publish() would have: nobody else made any, so each goes in as it is
static void publish_identity_link(void* arg, u32 kind, void const* subject, void const* value) {
    TyGraph* me = arg;
    if (value && graph_kind_class(kind) == GRAPH_IDENTITY) graph_link_insert(me, kind, subject, value);
}

static void publish_identities(TyGraph* me, Checker* checker) {
    InternTable* tables[2] = { &checker->types, &checker->links };
    SharedTable* shared[2] = { my types, my links };
    for (u32 t = 0; t < 2; t++) {
        for (u32 i = 0; i < tables[t]->cap; i++) {
            InternEntry const* entry = &tables[t]->entries[i];
            if (!entry->hash || !entry->block[0]) continue;
            u32 count;
            if (graph_kind_class(block_kind(tables[t], checker, entry, &count)) != GRAPH_IDENTITY) continue;
            void const** there;
            shared_table_insert(shared[t], entry->hash, entry->block, count, &there);
        }
    }
    link_table_each(&checker->node_links, publish_identity_link, me);
}

// (Debug: TTTT_DEBUG_UNSHARED=1, at each share's end: identities still private, which a constructor set with a plain
// store instead of intern_set(). Each is a type the other checkers make again: vite's KEY_NODE function types, TS9011s)
void graph_check_unshared(TyGraph* me, Checker* checker) {
    InternTable* tables[2] = { &checker->types, &checker->links };
    SharedTable* shared[2] = { my types, my links };
    u64 by_kind[256] = {0};
    for (u32 t = 0; t < 2; t++) {
        for (u32 i = 0; i < tables[t]->cap; i++) {
            InternEntry const* entry = &tables[t]->entries[i];
            if (!entry->hash || !entry->block[0]) continue;
            u32 count;
            u32 kind = block_kind(tables[t], checker, entry, &count);
            if (graph_kind_class(kind) != GRAPH_IDENTITY) continue;
            u64 const* key = (u64 const*)(entry->block + 1);
            if (count >= 0xffffu) key++;
            if (!shared_table_find(shared[t], entry->hash, key, count)) by_kind[kind & 255]++;
        }
    }
    for (u32 k = 0; k < 256; k++) {
        if (by_kind[k]) eprintln("graph: ", by_kind[k], " unshared identities of kind ", kind_name(k) ? kind_name(k) : "?");
    }
}

typedef struct LinkPublish {
    TyGraph* graph;
} LinkPublish;

static void publish_link(void* arg, u32 kind, void const* subject, void const* value) {
    TyGraph* me = ((LinkPublish*)arg)->graph;
    KindStats* stats = &my stats[256 + (kind & 255)];
    if (graph_kind_class(kind) == GRAPH_SCRATCH) {
        GRAPH_STAT_ADD(&stats->scratch, 1, __ATOMIC_RELAXED);
        return;
    }
    if (!value) {
        GRAPH_STAT_ADD(&stats->unfinished, 1, __ATOMIC_RELAXED);
        return;
    }
    if (shared_links_insert(my node_links, graph_link_key(kind, subject), value) == value) GRAPH_STAT_ADD(&stats->published, 1, __ATOMIC_RELAXED);
}

// (Pages are made by CAS: a loser frees its own)
static SharedWord* memo_at(TyGraph* me, u32 slot, bool create) {
    SharedWord* _Atomic* _Atomic* dir = &my memos[slot >> (LAZY_PAGE_BITS + LAZY_DIR_BITS)];
    SharedWord* _Atomic* d = atomic_load_explicit(dir, memory_order_acquire);
    if (!d) {
        if (!create) return nullptr;
        SharedWord* _Atomic* fresh = calloc(1 << LAZY_DIR_BITS, sizeof(SharedWord*));
        panic_if(!fresh, "out of memory");
        if (atomic_compare_exchange_strong(dir, &d, fresh)) d = fresh;
        else free((void*)fresh);
    }
    SharedWord* _Atomic* page = &d[(slot >> LAZY_PAGE_BITS) & ((1 << LAZY_DIR_BITS) - 1)];
    SharedWord* p = atomic_load_explicit(page, memory_order_acquire);
    if (!p) {
        if (!create) return nullptr;
        SharedWord* fresh = calloc(1 << LAZY_PAGE_BITS, sizeof(SharedWord));
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
}

unsigned graph_relation_get(TyGraph* me, u32 relation, u64 key) {
    unsigned ret = (unsigned)(uintptr_t)shared_links_get(my relations[relation], key);
    if (ret) GRAPH_STAT_ADD(&my relation_hits, 1, memory_order_relaxed);
    return ret;
}

void graph_relation_put(TyGraph* me, u32 relation, u64 key, unsigned value) {
    if (shared_links_insert(my relations[relation], key, (void const*)(uintptr_t)value) == (void const*)(uintptr_t)value) {
        GRAPH_STAT_ADD(&my relation_puts, 1, memory_order_relaxed);
    }
}

// Variance slots (graph.h). The slots are the graph's arena's: the first checker's, never recycled with the flag
VarianceSlot* graph_variance_slot(Checker* checker, void const* subject, bool alias, bool create) {
    TyGraph* me = checker->graph;
    // (Whenever others read what it measures, on threads or one checker after another: published by groups only, a
    // withheld member of a clean group measured again in a warm graph changed react-navigation at K16 T1)
    if (!me || !checker->graph_memo_on) return nullptr;
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

static _Atomic u64 variance_published, variance_adopted, variance_differ, variance_private, variance_poisoned, variance_groups, variance_groups_tainted;

static void memo_taint_mark(Checker* checker, void const* const* block);

bool graph_variances_publish(
    Checker* checker, void const** (*block_of)(Checker* checker, VarianceSlot const* slot), bool (*equal)(void const* a, void const* b), bool clean
) {
    bool all_published = true;
    GRAPH_STAT_ADD(&variance_groups, 1, memory_order_relaxed);
    if (!clean) GRAPH_STAT_ADD(&variance_groups_tainted, 1, memory_order_relaxed);
    for (u32 i = 0; i < checker->graph_variance_claims_len; i++) {
        VarianceSlot* slot = checker->graph_variance_claims[i];
        void const** block = block_of(checker, slot);
        void const* mine = block ? block[0] : nullptr;
        // (Not measured after all: a restart of another subject's measurement finished first and this one's never
        // ran. Left empty for whoever measures it)
        if (!mine) continue;
        uintptr_t seen = SW_EMPTY;
        if (__atomic_compare_exchange_n(&slot->value.v, &seen, clean ? (uintptr_t)mine : (uintptr_t)SW_POISON, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
            if (clean) {
                GRAPH_STAT_ADD(&variance_published, 1, memory_order_relaxed);
#ifdef TTTT_GRAPH_FINGERPRINT
                if (!slot->alias) graph_fp_result(TK_VARIANCES, slot->subject, mine, false);
#endif
                continue;
            }
            GRAPH_STAT_ADD(&variance_poisoned, 1, memory_order_relaxed);
        } else if (clean && seen != SW_POISON && equal((void const*)seen, mine)) {
            // (Pure data: the winner's from now on, nothing told the two apart)
            GRAPH_STAT_ADD(&variance_adopted, 1, memory_order_relaxed);
            block[0] = (void const*)seen;
            continue;
        } else if (!clean || seen == SW_POISON) {
            GRAPH_STAT_ADD(&variance_private, 1, memory_order_relaxed);
        } else {
            GRAPH_STAT_ADD(&variance_differ, 1, memory_order_relaxed);
            // (Two clean groups that disagree: something either read escaped the rule)
            if (getenv("TTTT_GRAPH_STATS")) {
                cstr name = slot->alias ? cstr_from("(an alias)") : ty_to_cstr(checker, slot->subject);
                unsigned char const* x = (unsigned char const*)mine;
                unsigned char const* y = (unsigned char const*)seen;
                // (Variances: u32 len, bool pending, the data from byte 5)
                cstr xs = cstr_init(), ys = cstr_init();
                for (u32 i = 0; i < *(u32 const*)x && i < 16; i++) {
                    cstr_append_fmt(&xs, " %u", x[5 + i]);
                    cstr_append_fmt(&ys, " %u", y[5 + i]);
                }
                eprintln("graph: variances of ", name, " differ from the published ones (worker ", (u32)checker->graph_worker, "):", xs, " vs", ys);
                c_drop(cstr, &xs, &ys);
                cstr_drop(&name);
            }
        }
        // Its own from now on: private-first lookups keep finding it, and reading it is a taint event
        memo_taint_mark(checker, (void const* const*)block);
        all_published = false;
    }
    checker->graph_variance_claims_len = 0;
    return all_published;
}

static void publish_declaration_memo(TyGraph* me, Checker* checker, u32 slot) {
    KindStats* stats = &my stats[GRAPH_STAT_MEMOS];
    LazyType lazy = { .tag = LAZY_TYPE_EVALUATED, .slot = slot };
    LazySlots* saved = current_lazy_slots;
    current_lazy_slots = &checker->lazy;
    LazyMemo const* memo = lazy_type_state_peek(&lazy);
    current_lazy_slots = saved;
    if (!memo || !memo->done) {
        GRAPH_STAT_ADD(&stats->unfinished, 1, __ATOMIC_RELAXED);
        return;
    }
    // (An immutable copy in the checker's arena, which lives as long as the graph: first wins, a loser's copy is unread)
    LazyMemo* copy = arena_new(&checker->arena, LazyMemo);
    *copy = (LazyMemo){ .evaluated = memo->evaluated, .context = memo->context, .evaluation_started = true, .done = true };
    uintptr_t seen = SW_EMPTY;
    if (__atomic_compare_exchange_n(&memo_at(me, slot, true)->v, &seen, (uintptr_t)copy, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
        GRAPH_STAT_ADD(&stats->published, 1, __ATOMIC_RELAXED);
    }
}

void const* graph_link_get(TyGraph const* me, u32 kind, void const* subject) {
    // (Alone on the graph, a checker has its own identities, the only ones published: ty_link_publish())
    if (__atomic_load_n(&my checkers, __ATOMIC_RELAXED) == 1) return nullptr;
    void const* ret = shared_links_get(my node_links, graph_link_key(kind, subject));
    if (ret) GRAPH_STAT_ADD(&((TyGraph*)me)->stats[256 + (kind & 255)].shared_hits, 1, __ATOMIC_RELAXED);
    return ret;
}

void graph_count_lost(TyGraph* me, u32 kind) {
    GRAPH_STAT_ADD(&my stats[kind < GRAPH_STAT_KINDS ? kind : 0].lost, 1, __ATOMIC_RELAXED);
}

void const* graph_link_insert(TyGraph* me, u32 kind, void const* subject, void const* value) {
    void const* there = shared_links_insert(my node_links, graph_link_key(kind, subject), value);
    if (there == value) GRAPH_STAT_ADD(&my stats[256 + (kind & 255)].published, 1, __ATOMIC_RELAXED);
    return there;
}

bool graph_memo_take(TyGraph* me, u32 slot, LazyMemo* memo) {
    SharedWord const* word = memo_at(me, slot, false);
    uintptr_t v = word ? sw_load(word) : SW_EMPTY;
    if (v <= SW_POISON) return false;
    LazyMemo const* shared = (LazyMemo const*)v;
    // (Only its value: the rest of the private memo is this checker's)
    memo->evaluated = shared->evaluated;
    memo->context = shared->context;
    memo->evaluation_started = true;
    memo->done = true;
    memo->tainted = false;
    GRAPH_STAT_ADD(&my stats[GRAPH_STAT_MEMOS].shared_hits, 1, __ATOMIC_RELAXED);
    return true;
}

// Memo publication at the computation's end (graph.h)

// (Only once there is someone to read them: a graph of one checker, `--checkers 1`, publishes nothing)
bool graph_memo_sharing(Checker const* checker) {
    return checker->graph_memo_on;
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
    u32 variance_depth_scratch; // Checker.variance_depth at the put (TTTT_DEBUG_DISAGREE)
} GraphMemoLogEntry;
typedef struct Disagreement { void const* mine; void const* winner; u64 k0, k1; u32 kind; bool iprop; u32 depth; u32 worker; } Disagreement;
static Disagreement disagree_list[1 << 16];
static u32 disagree_len;
static pthread_mutex_t disagree_lock = PTHREAD_MUTEX_INITIALIZER;
static char const* kind_name(u32 kind);
// TTTT_DEBUG_DISAGREE=1 (--shared-graph): memo entries a checker computed differently from the one another checker
// published first (conditional instances and intersection properties whose types print differently), at the end
void graph_print_disagreements(Checker* checker) {
    for (u32 i = 0; i < disagree_len; i++) {
        Disagreement d = disagree_list[i];
        Ty const* a = nullptr;
        Ty const* b = nullptr;
        if (d.iprop) {
            FoundProp const* x = d.mine;
            FoundProp const* y = d.winner;
            if (x->found != y->found || x->flags != y->flags) {
                fprintf(stderr, "IPROPDIS w%u d%u found %u/%u flags %x/%x\n", d.worker, d.depth, x->found, y->found, x->flags, y->flags);
            }
            a = x->found ? x->type : nullptr;
            b = y->found ? y->type : nullptr;
        } else if (d.kind == KEY_CONDITIONAL_INSTANCE || d.kind == KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT) {
            a = ((Ty const* const*)d.mine)[0];
            b = ((Ty const* const*)d.winner)[0];
        } else {
            continue;
        }
        if (!a || !b) continue;
        cstr x = ty_to_cstr(checker, a), y = ty_to_cstr(checker, b);
        if (!cstr_eq(&x, &y)) {
            Ty const* subject = d.iprop ? lazy_slots_type(&checker->lazy, (u32)(d.k0 >> 1)) : nullptr;
            cstr sub = subject ? ty_to_cstr(checker, subject) : cstr_init();
            fprintf(stderr, "DISAGREE %s w%u d%u %s.%s\n  mine   %.400s\n  winner %.400s\n", d.iprop ? "iprop" : kind_name(d.kind), d.worker, d.depth,
                cstr_str(&sub), d.iprop ? ((Atom const*)(uintptr_t)d.k1)->str : "", cstr_str(&x), cstr_str(&y));
            cstr_drop(&sub);
        }
        c_drop(cstr, &x, &y);
    }
}

static SharedTable* memo_log_table(TyGraph* me, uintptr_t tag) {
    return tag == 0 ? my types : tag == 1 ? my links : my intersection_props;
}

// Plan 86 stage 4f: which memo entries are clean (graph.h). Per checker: its memo kinds' misses by (table, hash),
// direct-mapped. A miss pushed out of its slot leaves its start in a floor by a coarser hash, which a put of a key not in
// the miss table takes as its start: a key whose miss was pushed out is never taken for clean (at worst another key of
// the bucket is taken for tainted). Forgotten at each file's start, when nothing is in flight. The tainted entries: a
// bitmap by block address in front of an exact set
enum { LOG_TAINTED = 4 };
static _Atomic u64 memo_stat_clean, memo_stat_tainted, memo_stat_floor, memo_stat_hits;

static struct MemoTaint* memo_taint_of(Checker* checker) {
    if (!checker->memo_taint) {
        checker->memo_taint = calloc(1, sizeof(struct MemoTaint));
        panic_if(!checker->memo_taint, "out of memory");
        checker->memo_taint->gen = 1;
    }
    return checker->memo_taint;
}

// Memo kinds whose value is a fresh object no other key hash-conses (a synthetic type parameter, an evolving array, a
// reverse mapped object, a lowered node's type): withheld, every worker would make its own and tell them apart. They
// publish whatever their computation saw, like identities (plan 86 stage 4f; the rest are values, listed in plan 86)
static bool memo_identity_valued(u32 kind) {
    switch (kind) {
    case KEY_MAPPED_KEY_PARAM: case KEY_EVOLVING_ARRAY: case KEY_REVERSE_MAPPED: case KEY_UNION_NODE:
    // (A conditional instance's result can hold objects its inference made fresh, a nested reverse mapped type:
    // withheld, react-navigation's TS2344/TS2345 at K16 T1, each checker making its own)
    case KEY_CONDITIONAL_INSTANCE: case KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT:
        return true;
    default:
        return false;
    }
}

static u32 memo_floor_index(void const* table, u64 hash) {
    return (u32)(memo_taint_mix(hash ^ (u64)(uintptr_t)table) >> 54) & (MEMO_FLOOR_SLOTS - 1);
}

void graph_memo_miss(Checker* checker, InternTable const* table, u64 hash) {
    struct MemoTaint* t = memo_taint_of(checker);
    MemoMiss* m = &t->misses[memo_taint_mix(hash ^ (u64)(uintptr_t)table) & (MEMO_MISS_SLOTS - 1)];
    if (m->gen == t->gen && m->hash == hash && m->table == table) return; // (The earlier miss: its computation started first)
    if (m->gen == t->gen && m->hash) {
        u32 f = memo_floor_index(m->table, m->hash);
        if (t->floor_gen[f] != t->gen || m->start < t->floor_start[f]) {
            t->floor_gen[f] = t->gen;
            t->floor_start[f] = m->start;
        }
    }
    *m = (MemoMiss){ table, hash, graph_taint_now, t->gen };
}

// Where the computation of a key put now started
static u64 memo_start(struct MemoTaint* t, InternTable const* table, u64 hash) {
    MemoMiss* m = &t->misses[memo_taint_mix(hash ^ (u64)(uintptr_t)table) & (MEMO_MISS_SLOTS - 1)];
    if (m->gen == t->gen && m->hash == hash && m->table == table) {
        m->hash = 0;
        return m->start;
    }
    u32 f = memo_floor_index(table, hash);
    if (t->floor_gen[f] == t->gen && t->floor_start[f] != graph_taint_now) {
        GRAPH_STAT_ADD(&memo_stat_floor, 1, memory_order_relaxed);
        return t->floor_start[f];
    }
    return graph_taint_now;
}

static bool memo_taint_has(struct MemoTaint const* t, uintptr_t key) {
    if (!memo_taint_maybe(t, key)) return false;
    for (u32 i = (u32)(memo_taint_mix((u64)key) >> 32) & (t->cap - 1); t->blocks[i]; i = (i + 1) & (t->cap - 1)) {
        if (t->blocks[i] == key) return true;
    }
    return false;
}

static void memo_taint_mark(Checker* checker, void const* const* block) {
    struct MemoTaint* t = memo_taint_of(checker);
    uintptr_t key = (uintptr_t)block;
    u64 h = memo_taint_mix((u64)key);
    t->bits[(h >> 6) & (MEMO_TAINT_BITS / 64 - 1)] |= 1ull << (h & 63);
    if ((t->len + 1) * 2 > t->cap) {
        u32 cap = t->cap ? t->cap * 2 : 256;
        uintptr_t* bigger = calloc(cap, sizeof(uintptr_t));
        panic_if(!bigger, "out of memory");
        for (u32 i = 0; i < t->cap; i++) {
            if (!t->blocks[i]) continue;
            u32 j = (u32)(memo_taint_mix((u64)t->blocks[i]) >> 32) & (cap - 1);
            while (bigger[j]) j = (j + 1) & (cap - 1);
            bigger[j] = t->blocks[i];
        }
        free(t->blocks);
        t->blocks = bigger;
        t->cap = cap;
    }
    u32 i = (u32)(h >> 32) & (t->cap - 1);
    while (t->blocks[i] && t->blocks[i] != key) i = (i + 1) & (t->cap - 1);
    if (t->blocks[i]) return;
    t->blocks[i] = key;
    t->len++;
    GRAPH_STAT_ADD(&memo_stat_tainted, 1, memory_order_relaxed);
    checker->types.taint = checker->links.taint = checker->intersection_props.taint = t;
}

void graph_memo_taint_block(Checker* checker, void const* const* block) {
    memo_taint_mark(checker, block);
}

void graph_link_tainted(Checker* checker, u64 key) {
    memo_taint_mark(checker, (void const* const*)(uintptr_t)key);
}

void graph_memo_hit_slow(struct MemoTaint const* t, uintptr_t key) {
    if (memo_taint_has(t, key)) {
        GRAPH_STAT_ADD(&memo_stat_hits, 1, memory_order_relaxed);
        TAINT_EVENT(TAINT_READ_TAINTED);
    }
}

void graph_memo_hit_cold(MemoTaint const* t, void const* const* block) {
    graph_memo_hit(t, block);
}

// (The entries put since the last event: those still without a value were in flight across this one)
void graph_memo_sweep(Checker* checker) {
    struct MemoTaint* t = checker->memo_taint;
    for (u32 i = 0; i < t->pending_len; i++) {
        if (!t->pending[i][0]) memo_taint_mark(checker, t->pending[i]);
    }
    t->pending_len = 0;
}

static void memo_pending_push(struct MemoTaint* t, void const* const* block) {
    if (t->pending_len == t->pending_cap) {
        // (Those with their value are done, before any event since: clean)
        u32 kept = 0;
        for (u32 i = 0; i < t->pending_len; i++) {
            if (!t->pending[i][0]) t->pending[kept++] = t->pending[i];
        }
        t->pending_len = kept;
        if (!t->pending_cap || kept * 2 > t->pending_cap) {
            t->pending_cap = t->pending_cap ? t->pending_cap * 2 : 256;
            t->pending = realloc(t->pending, sizeof(void const* const*) * t->pending_cap);
            panic_if(!t->pending, "out of memory");
        }
    }
    t->pending[t->pending_len++] = block;
}

void graph_memo_file_start(Checker* checker) {
    if (checker->memo_taint) checker->memo_taint->gen++;
}

static void memo_taint_drop(Checker* checker) {
    if (!checker->memo_taint) return;
    free(checker->memo_taint->blocks);
    free(checker->memo_taint->pending);
    free(checker->memo_taint);
    checker->memo_taint = nullptr;
    checker->types.taint = checker->links.taint = checker->intersection_props.taint = nullptr;
}

void graph_memo_log_put(Checker* checker, InternTable const* table, void const** block, u32 kind) {
    uintptr_t tag;
    if (table == &checker->types) tag = 0;
    else if (table == &checker->links) tag = 1;
    else tag = 2;
    if (tag != 2 && graph_kind_class(kind) != GRAPH_MEMO) return;
    // (Variances on threads go through their slots, graph_variance_slot(), as groups: relate.c)
    if ((kind == KEY_VARIANCES || kind == KEY_ALIAS_VARIANCES) && tag == 1) return;
    // (Nobody to read them: one checker on the graph, `--checkers 1`, publishes nothing)
    if (!graph_memo_sharing(checker)) return;
    // (Clean so far: no event since its miss, and pending until its value is there. Tainted: the checker's own from now
    // on, a hit of it an event)
    bool identity = tag != 2 && memo_identity_valued(kind);
    bool tainted = false;
    if (!identity) {
        struct MemoTaint* t = memo_taint_of(checker);
        tainted = memo_start(t, table, ((u64 const*)block)[-1]) != graph_taint_now;
        if (tainted) memo_taint_mark(checker, (void const* const*)block);
        else {
            memo_pending_push(t, (void const* const*)block);
            GRAPH_STAT_ADD(&memo_stat_clean, 1, memory_order_relaxed);
        }
    }
    if (!checker->graph_memo_log_limit) checker->graph_memo_log_limit = 64;
    // (Before the new one goes in, whose block isn't filled yet. Entries still without a value stay: the limit grows
    // with them, a deep computation doesn't flush every put)
    if (checker->graph_memo_log_len >= checker->graph_memo_log_limit) graph_memo_flush(checker);
    if (checker->graph_memo_log_len == checker->graph_memo_log_cap) {
        checker->graph_memo_log_cap = checker->graph_memo_log_cap ? checker->graph_memo_log_cap * 2 : 256;
        checker->graph_memo_log = realloc(checker->graph_memo_log, sizeof(GraphMemoLogEntry) * checker->graph_memo_log_cap);
        panic_if(!checker->graph_memo_log, "out of memory");
    }
    checker->graph_memo_log[checker->graph_memo_log_len++] = (GraphMemoLogEntry){ (uintptr_t)block | tag | (tainted ? LOG_TAINTED : 0), checker->instantiation_limit_hits,
        checker->variance_depth };
}

void graph_memo_log_unput(Checker* checker, void const** block) {
    u32 n = checker->graph_memo_log_len;
    if (n && (checker->graph_memo_log[n - 1].entry & ~(uintptr_t)7) == (uintptr_t)block) checker->graph_memo_log_len--;
}

void graph_memo_flush(Checker* checker) {
    TyGraph* me = checker->graph;
    if (!me) return;
    graph_sched_point(false);
    if (__builtin_expect(__atomic_load_n(&debug_memo_publish, __ATOMIC_RELAXED) < 0, 0)) {
        __atomic_store_n(&debug_memo_publish, getenv("TTTT_DEBUG_MEMO_PUBLISH") != nullptr, __ATOMIC_RELAXED);
    }
    u32 kept = 0;
    for (u32 i = 0; i < checker->graph_memo_log_len; i++) {
        GraphMemoLogEntry logged = checker->graph_memo_log[i];
        uintptr_t entry = logged.entry;
        void const** block = (void const**)(entry & ~(uintptr_t)7);
        if (!block[0]) {
            checker->graph_memo_log[kept++] = logged;
            continue;
        }

        // (The instantiation limits ran out while it may have been computed (TS2589): what it is was cut short in this
        // worker's history, which the others don't share. It stays the worker's own)
        if (logged.limit_hits != checker->instantiation_limit_hits) {
            GRAPH_STAT_ADD(&my stats[GRAPH_STAT_KINDS - 1].tainted, 1, __ATOMIC_RELAXED);
            continue;
        }
        u64 hash = ((u64 const*)block)[-1];
        u32 count = (u32)(hash >> 48);
        u64 const* key = (u64 const*)(block + 1);
        if (count == 0xffffu) count = (u32)*key++;
        u32 kind = (entry & 3) == 2 ? GRAPH_KIND_INTERSECTION_PROPS : count ? (u32)(key[0] & 255) : 0;
        KindStats* stats = &my stats[kind];
        // (Its computation was tainted: the checker's own, plan 86 stage 4f)
        if ((entry & LOG_TAINTED) || (checker->memo_taint && memo_taint_has(checker->memo_taint, (uintptr_t)block))) {
            GRAPH_STAT_ADD(&stats->tainted, 1, __ATOMIC_RELAXED);
            continue;
        }
#ifdef TTTT_GRAPH_FINGERPRINT
        if ((entry & 3) != 2) graph_fp_memo(kind, block, key, count);
#endif
        void const** there;
        if (shared_table_insert(memo_log_table(me, entry & 3), hash, block, count, &there)) {
            GRAPH_STAT_ADD(&stats->published, 1, __ATOMIC_RELAXED);
            if (__atomic_load_n(&debug_memo_publish, __ATOMIC_RELAXED) > 0) debug_memo_published(block);
        } else if (there != block) {
            // (Another checker's came first: this one reads that from now on)
            void const* winner = __atomic_load_n(there, __ATOMIC_ACQUIRE);
            // (Relaxed atomics: every worker's first loss reads the knob, TSan)
            static int debug_disagree_ = -1;
            int debug_disagree = __atomic_load_n(&debug_disagree_, __ATOMIC_RELAXED);
            if (debug_disagree < 0) {
                debug_disagree = getenv("TTTT_DEBUG_DISAGREE") != nullptr;
                __atomic_store_n(&debug_disagree_, debug_disagree, __ATOMIC_RELAXED);
            }
            if (winner != block[0] && debug_disagree) {
                pthread_mutex_lock(&disagree_lock);
                if (disagree_len < c_arraylen(disagree_list)) {
                    disagree_list[disagree_len++] = (Disagreement){ block[0], winner, key[0], key[1], kind, (entry & 3) == 2, logged.variance_depth_scratch, checker->graph_worker };
                }
                pthread_mutex_unlock(&disagree_lock);
            }
            block[0] = winner;
            GRAPH_STAT_ADD(&stats->lost, 1, __ATOMIC_RELAXED);
        }
    }
    checker->graph_memo_log_len = kept;
    u32 limit = checker->graph_memo_log_limit ? checker->graph_memo_log_limit : 64;
    while (kept * 2 > limit) limit *= 2;
    while (limit > 64 && kept * 8 < limit) limit /= 2;
    checker->graph_memo_log_limit = limit;
}


void graph_memo_done(Checker* checker, u32 slot, bool tainted) {
    if (!graph_memo_sharing(checker)) return;
    if (tainted) {
        GRAPH_STAT_ADD(&checker->graph->stats[GRAPH_STAT_MEMOS].tainted, 1, __ATOMIC_RELAXED);
        return;
    }
    publish_declaration_memo(checker->graph, checker, slot);
}

char const* graph_kind_name(u32 kind) {
    return kind_name(kind);
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
    u64 pairs = 0;
    for (u32 w = 1; w <= my checkers && w < 64; w++) if (my workers[w]) pairs += my workers[w]->relation_pairs;
    eprintln("graph: relation pairs computed ", pairs);
    u64 deep = 0;
    for (u32 w = 1; w <= my checkers && w < 64; w++) if (my workers[w]) deep += my workers[w]->deep_cuts;
    eprintln("graph: deep-nesting cuts ", deep, " (taint events, plan 86 stage 4f)");
    eprintln("graph: variance groups ", (u64)variance_groups, " (tainted ", (u64)variance_groups_tainted, "), slots published ",
        (u64)variance_published, ", adopted ", (u64)variance_adopted, ", differing ", (u64)variance_differ, ", poisoned ",
        (u64)variance_poisoned, ", tainted after another's ", (u64)variance_private);
    eprintln("graph: memo entries clean ", (u64)memo_stat_clean, ", tainted ", (u64)memo_stat_tainted, " (by a pushed-out miss ",
        (u64)memo_stat_floor, "), tainted hits ", (u64)memo_stat_hits);
    overlay_print_stats((Checker* const*)my workers + 1, my checkers < 63 ? my checkers : 63);
    eprintln("graph: relations shared ", (u64)atomic_load(&((TyGraph*)me)->relation_puts), ", hits ", (u64)atomic_load(&((TyGraph*)me)->relation_hits));
    eprintln("graph: ", my checkers, " checkers, ", shared_table_len(my types), " types entries, ", shared_table_len(my links),
        " links, ", shared_table_len(my intersection_props), " intersection props, ", shared_links_len(my node_links),
        " node links shared");
    for (u32 w = 1; w <= my checkers && w < 64; w++) {
        Checker const* c = my workers[w];
        if (c && c->instantiation_limit_hits) eprintln("graph: worker ", w, " instantiation limit hits ", c->instantiation_limit_hits);
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
}
