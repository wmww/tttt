// --shared-graph: the type graph checkers share, see graph.h and APPROACH.md
#include "ty/graph.h"
#include "ty/ty_internal.h"
#include "lazy_type.h"
#include "program.h"
#include <time.h>

// The state handed to whichever checker is current (graph_enter()): sequentially one checker at a time makes atoms and
// types. In 4d: shared structures of their own (4c's global atoms and ids)
typedef struct GraphHeld {
    u32 next_type_id;
    Ty const** types[1 << LAZY_TYPE_DIR_BITS]; // The id -> Ty table's chunks (LazySlots.types)
    Ty const* array_target;
    Ty const* auto_array;
    Ty const* readonly_array_target;
    TyList tuple_params;
    struct Signature** unknown_signature;
    Ty const* global_types[14];
    bool global_types_resolved[14];
    struct IterationState const* iteration;
    Ty const* disposable_targets[2];
    bool disposable_looked_up[2];
} GraphHeld;

typedef struct KindStats {
    u64 shared_hits; // Lookups a checker's own table missed and the shared one had
    u64 published; // Entries published
    u64 tainted; // Entries of memo kinds kept private because their computation was tainted
    u64 scratch; // Entries of scratch kinds (never published)
    u64 unfinished; // Entries without a value when their checker's share ended
} KindStats;

// What a checker made, by GraphClass and by kind (256: node_links kinds 256 + kind)
enum { GRAPH_STAT_KINDS = 512, GRAPH_STAT_MEMOS = 500 };

struct TyGraph {
    Checker* owner; // The checker holding `held` (current), null: the graph holds it
    GraphHeld held;
    // The first checker's, which every other one shares (checker_new() makes them only for the first)
    Ty const* intrinsics[TY_INTRINSIC_COUNT];
    Ty const* typeof_type;
    struct Atom const* atom_constructor;
    struct Atom const* atom_prototype;
    struct Atom const* atom_bind;
    // The shared tables
    InternTable types;
    InternTable links;
    InternTable intersection_props;
    LinkTable node_links;
    LazyMemo** memos[1 << LAZY_TOP_BITS]; // Declaration memos by slot, like LazySlots.top
    u64 memo_hits;
    u32 first_id; // The current checker's first type id: what it makes from there on is its own
    KindStats stats[GRAPH_STAT_KINDS];
    u32 checkers;
};


GraphClass graph_kind_class(u32 kind) {
    switch (kind) {
    // Roots (per symbol, per declaration) and identities (constructors: the value is what the key is)
    case KEY_TYPE_PARAM: case KEY_SYMBOL_LINKS: case KEY_GENERIC_PARAMS: case KEY_NAMESPACE: case KEY_TYPE_ONLY_EXPORT:
    case KEY_MERGED_VALUE: case KEY_DECL_GROUP: case KEY_DECL_GROUP_OF: case KEY_PRIVATE_NAME:
    case KEY_PRIVATE_NAME_CLASS: case KEY_NAMESPACE_IMPORT: case KEY_CLASS_MEMBER_SYMBOLS:
    case KEY_LITERAL: case KEY_UNION: case KEY_INTERSECTION: case KEY_REFERENCE: case KEY_TUPLE_TARGET: case KEY_KEYOF:
    case KEY_INDEXED: case KEY_CONDITIONAL: case KEY_SUBSTITUTION: case KEY_ANONYMOUS_INSTANCE: case KEY_UNIQUE_SYMBOL:
    case KEY_TEMPLATE_LITERAL: case KEY_STRING_MAPPING: case KEY_NODE: case KEY_DEFERRED_OUTER:
    case KEY_DEFERRED_REFERENCE: case KEY_STRUCTURAL: case KEY_RESTRICTIVE_PARAM: case KEY_MARKER: case KEY_CHECK_MARKER:
    case KEY_FUNCTION: case KEY_UNIQUE_SYMBOL_NAME: case KEY_UNDEFINED_PROPERTY: case KEY_FUNCTION_RETURN:
    case KEY_IMPLEMENTATION_SIGNATURE: case KEY_SIGNATURE_INSTANCE: case KEY_SIGNATURE_TYPE:
    case KEY_MODULE_CLONE: case KEY_FRESH_LITERAL:
        return GRAPH_IDENTITY;
    // Computed values (some of them computed while lowering or instantiating: KEY_ALIAS_INSTANCE, KEY_UNION_NODE,
    // the conditional instances)
    case KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT: case KEY_CONDITIONAL_INSTANCE: case KEY_ALIAS_INSTANCE:
    case KEY_UNION_NODE: case KEY_ALIAS_VARIANCES:
    case KEY_REGULAR_UNION: case KEY_WIDENED_OBJECTS: case KEY_REGULAR_OBJECT_LITERAL: case KEY_MAPPED_KEY_PARAM:
    case KEY_KEYOF_PROPERTIES: case KEY_SIMPLIFIED: case KEY_NORMALIZED_INTERSECTION: case KEY_MAPPED_FILTERING:
    case KEY_PROP_MAPPED_TYPE: case KEY_PROP_WRITE_TYPE: case KEY_UNION_ACCESS: case KEY_BASE_CONSTRAINT:
    case KEY_INFERRED_TRUE_BRANCH: case KEY_DISTRIBUTION_DEPENDENT: case KEY_DISTRIBUTIVE_CONSTRAINT:
    case KEY_IMPLIED_CONSTRAINT: case KEY_TYPE_VARIABLE_REDUCTION: case KEY_DEFINITELY_RELATED:
    case KEY_CANONICAL_SIGNATURE: case KEY_BASE_SIGNATURE: case KEY_VARIANCES: case KEY_ALIAS_MARKER:
    case KEY_CHECK_MARKER_REF: case KEY_ITERATION_TYPES: case KEY_WIDENED_NULLABLE:
    case KEY_INTERSECTION_PROP_CONTEXT: case KEY_SYNTHETIC_PROP_PARTS: case KEY_UNION_PAIR:
    case KEY_INTERSECTION_OF: case KEY_REVERSE_MAPPED: case KEY_UNION_KEY_NAME: case KEY_UNION_KEY_MEMBER: case KEY_FLOW_NOT_UNIT:
    case KEY_EVOLVING_ARRAY: case KEY_REDUCED_UNION: case KEY_FUNCTION_FREE_PARAMS: case KEY_FUNCTION_TYPE_MENTIONS:
    case KEY_ARRAY_THIS_PROPS: case KEY_TUPLE_NAME_ORDER: case KEY_INDEX_NAME_ORDER: case KEY_DECIMAL_ORDER:
    case KEY_THISLESS_SIGNATURES: case KEY_INSTANTIATION_QUERY: case GRAPH_KIND_INTERSECTION_PROPS:
        return GRAPH_MEMO;
    // KEY_GENERIC_RELATION numbers relations per checker; the rest is per expression, per call, flow, inference,
    // report-once
    default:
        return GRAPH_SCRATCH;
    }
}

static void held_take(GraphHeld* held, Checker const* from) {
    held->next_type_id = from->next_type_id;
    memcpy(held->types, from->lazy.types, sizeof(held->types));
    held->array_target = from->array_target;
    held->auto_array = from->auto_array;
    held->readonly_array_target = from->readonly_array_target;
    held->tuple_params = from->tuple_params;
    held->unknown_signature = from->unknown_signature;
    memcpy(held->global_types, from->global_types, sizeof(held->global_types));
    memcpy(held->global_types_resolved, from->global_types_resolved, sizeof(held->global_types_resolved));
    held->iteration = from->iteration;
    memcpy(held->disposable_targets, from->disposable_targets, sizeof(held->disposable_targets));
    memcpy(held->disposable_looked_up, from->disposable_looked_up, sizeof(held->disposable_looked_up));
}

static void held_give(GraphHeld const* held, Checker* to) {
    to->next_type_id = held->next_type_id;
    memcpy(to->lazy.types, held->types, sizeof(held->types));
    to->array_target = held->array_target;
    to->auto_array = held->auto_array;
    to->readonly_array_target = held->readonly_array_target;
    to->tuple_params = held->tuple_params;
    to->unknown_signature = held->unknown_signature;
    memcpy(to->global_types, held->global_types, sizeof(held->global_types));
    memcpy(to->global_types_resolved, held->global_types_resolved, sizeof(held->global_types_resolved));
    to->iteration = held->iteration;
    memcpy(to->disposable_targets, held->disposable_targets, sizeof(held->disposable_targets));
    memcpy(to->disposable_looked_up, held->disposable_looked_up, sizeof(held->disposable_looked_up));
}

static void graph_link_tables(TyGraph* me, Checker* checker) {
    checker->graph = me;
    checker->types.shared = &my types;
    checker->links.shared = &my links;
    checker->intersection_props.shared = &my intersection_props;
}

TyGraph* graph_new(Checker* first) {
    TyGraph* me = calloc(1, sizeof(TyGraph));
    panic_if(!me, "out of memory");
    memcpy(my intrinsics, first->intrinsics, sizeof(my intrinsics));
    my typeof_type = first->typeof_type;
    my atom_constructor = first->atom_constructor;
    my atom_prototype = first->atom_prototype;
    my atom_bind = first->atom_bind;
    graph_link_tables(me, first);
    my checkers = 1;
    // (It holds the state now; the graph's copy is taken when it stops being current)
    my owner = first;
    if (current_checker != first) graph_enter(me, nullptr);
    return me;
}

void graph_adopt(TyGraph* me, Checker* checker) {
    memcpy(checker->intrinsics, my intrinsics, sizeof(my intrinsics));
    checker->typeof_type = my typeof_type;
    checker->atom_constructor = my atom_constructor;
    checker->atom_prototype = my atom_prototype;
    checker->atom_bind = my atom_bind;
    graph_link_tables(me, checker);
    my checkers++;
}

void graph_enter(TyGraph* me, Checker* checker) {
    if (my owner == checker) return;
    if (my owner) held_take(&my held, my owner);
    if (checker) held_give(&my held, checker);
    my owner = checker;
    if (checker) my first_id = checker->next_type_id;
}

void graph_release(TyGraph* me, Checker* checker) {
    if (my owner == checker) graph_enter(me, nullptr);
    // (The graph's: freed by graph_drop())
    memset(checker->lazy.types, 0, sizeof(checker->lazy.types));
    free(checker->graph_memo_slots);
    checker->graph_memo_slots = nullptr;
}

void graph_drop(TyGraph* me) {
    if (!me) return;
    if (my owner) graph_release(me, my owner);
    for (size_t i = 0; i < c_arraylen(my held.types); i++) free(my held.types[i]);
    free(my types.entries);
    free(my links.entries);
    free(my intersection_props.entries);
    free(my node_links.entries);
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
    checker->graph->stats[kind].shared_hits++;
}

#ifdef TTTT_GRAPH_TAINT
// The cheap taint (taint.h's non-TTTT_TAINT macros): one thread, so plain statics besides the counter

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

static bool mark_has(u64 key) {
    u64 h = mix_key(key);
    if (!(graph_mark_bits[(h >> 6) & (c_arraylen(graph_mark_bits) - 1)] >> (h & 63) & 1)) return false;
    for (u32 i = (u32)(h >> 20) & (marks.cap - 1);; i = (i + 1) & (marks.cap - 1)) {
        if (marks.keys[i] == key) return true;
        if (!marks.keys[i]) return false;
    }
}

static void mark_add(u64 key) {
    if (!key || mark_has(key)) return;
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
    graph_mark_bits[(h >> 6) & (c_arraylen(graph_mark_bits) - 1)] |= 1ull << (h & 63);
    graph_taint_marks = true;
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
static MissRecord miss_ring[MISS_RING];
static u32 miss_next;

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
static PendingPut* pending;
static u32 pending_cap;

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
        tainted_by_kind[kind]++;
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
    taint_events_seen++;
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
    tainted_by_kind[kind < TK_COUNT ? kind : 0]++;
    // (Results kept on a type: is it one an earlier checker made, which the next ones get as it is?)
    bool on_type = kind == TK_MEMBERS || kind == TK_DECLARED_RAW || kind == TK_PARAM_CONSTRAINT;
    if (on_type && subject && ((Ty const*)subject)->id < checker->graph->first_id) tainted_shared_by_kind[kind]++;
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

static u32 block_kind(InternTable const* table, Checker const* checker, InternEntry const* entry, u32* count) {
    u32 n = (u32)(entry->hash >> 48);
    u64 const* key = (u64 const*)(entry->block + 1);
    if (n == 0xffffu) n = (u32)*key++;
    *count = n;
    if (table == &checker->intersection_props) return GRAPH_KIND_INTERSECTION_PROPS;
    return n ? (u32)(key[0] & 255) : 0;
}

static void publish_table(TyGraph* me, Checker* checker, InternTable* private_table, InternTable* shared) {
    intern_table_reserve(shared, private_table->len);
    for (u32 i = 0; i < private_table->cap; i++) {
        InternEntry const* entry = &private_table->entries[i];
        if (!entry->hash) continue;
        u32 count;
        u32 kind = block_kind(private_table, checker, entry, &count);
        KindStats* stats = &my stats[kind];
        GraphClass class = graph_kind_class(kind);
        if (class == GRAPH_SCRATCH) {
            stats->scratch++;
            continue;
        }
        if (!entry->block[0]) {
            stats->unfinished++;
            continue;
        }
        if (class == GRAPH_MEMO && entry_tainted(kind, entry->block)) {
            stats->tainted++;
            continue;
        }
        if (intern_table_has(shared, entry, count)) continue;
        intern_table_publish(shared, entry, count);
        stats->published++;
    }
}

typedef struct LinkPublish {
    TyGraph* graph;
} LinkPublish;

static void publish_link(void* arg, u32 kind, void const* subject, void const* value) {
    TyGraph* me = ((LinkPublish*)arg)->graph;
    KindStats* stats = &my stats[256 + (kind & 255)];
    if (graph_kind_class(kind) == GRAPH_SCRATCH) {
        stats->scratch++;
        return;
    }
    if (!value) {
        stats->unfinished++;
        return;
    }
    if (graph_kind_class(kind) == GRAPH_MEMO && link_tainted(kind, subject, value)) {
        stats->tainted++;
        return;
    }
    if (link_table_get(&my node_links, kind, subject)) return;
    link_table_set(&my node_links, kind, subject, value);
    stats->published++;
}

static LazyMemo* memo_at(TyGraph* me, u32 slot, bool create) {
    LazyMemo*** dir = &my memos[slot >> (LAZY_PAGE_BITS + LAZY_DIR_BITS)];
    if (!*dir) {
        if (!create) return nullptr;
        *dir = calloc(1 << LAZY_DIR_BITS, sizeof(LazyMemo*));
        panic_if(!*dir, "out of memory");
    }
    LazyMemo** page = &(*dir)[(slot >> LAZY_PAGE_BITS) & ((1 << LAZY_DIR_BITS) - 1)];
    if (!*page) {
        if (!create) return nullptr;
        *page = calloc(1 << LAZY_PAGE_BITS, sizeof(LazyMemo));
        panic_if(!*page, "out of memory");
    }
    return &(*page)[slot & ((1 << LAZY_PAGE_BITS) - 1)];
}

static double now_ms(void) {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

void graph_publish(TyGraph* me, Checker* checker) {
    double t0 = now_ms();
    publish_table(me, checker, &checker->types, &my types);
    double t1 = now_ms();
    publish_table(me, checker, &checker->links, &my links);
    double t2 = now_ms();
    publish_table(me, checker, &checker->intersection_props, &my intersection_props);
    double t3 = now_ms();
    LinkPublish arg = { me };
    link_table_reserve(&my node_links, checker->node_links.len);
    link_table_each(&checker->node_links, publish_link, &arg);
    double t4 = now_ms();
    if (getenv("TTTT_GRAPH_STATS")) eprintln("graph: publish types ", t1 - t0, " links ", t2 - t1, " iprops ", t3 - t2, " node links ", t4 - t3);
    KindStats* stats = &my stats[GRAPH_STAT_MEMOS];
    for (u32 i = 0; i < checker->graph_memo_len; i++) {
        u32 slot = checker->graph_memo_slots[i] & ~(1u << 31);
        if (checker->graph_memo_slots[i] >> 31 && !publish_tainted(GRAPH_STAT_MEMOS)) {
            stats->tainted++;
            continue;
        }
        LazyType lazy = { .tag = LAZY_TYPE_EVALUATED, .slot = slot };
        LazySlots* saved = current_lazy_slots;
        current_lazy_slots = &checker->lazy;
        LazyMemo const* memo = lazy_type_state_peek(&lazy);
        current_lazy_slots = saved;
        if (!memo || !memo->done) {
            stats->unfinished++;
            continue;
        }
        LazyMemo* shared = memo_at(me, slot, true);
        if (shared->done) continue;
        *shared = *memo;
        shared->hits = 0;
        stats->published++;
    }
    checker->graph_memo_len = 0;
}

void const* graph_link_get(TyGraph const* me, u32 kind, void const* subject) {
    void const* ret = link_table_get(&my node_links, kind, subject);
    if (ret) ((TyGraph*)me)->stats[256 + (kind & 255)].shared_hits++;
    return ret;
}

bool graph_memo_take(TyGraph* me, u32 slot, LazyMemo* memo) {
    LazyMemo const* shared = memo_at(me, slot, false);
    if (!shared || !shared->done) return false;
    *memo = *shared;
    my stats[GRAPH_STAT_MEMOS].shared_hits++;
    return true;
}

void graph_memo_done(Checker* checker, u32 slot, bool tainted) {
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
    eprintln("graph: ", my checkers, " checkers, ", (i64)my types.len, " types entries, ", (i64)my links.len, " links, ",
        (i64)my intersection_props.len, " intersection props, ", (i64)my node_links.len, " node links shared");
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
            tainted_shared_by_kind[k], ")");
    }
#else
    eprintln("graph: taint not compiled in (-DTTTT_GRAPH_TAINT): memo kinds published whatever their computation saw");
#endif
}
