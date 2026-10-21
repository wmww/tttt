#pragma once

// Plan 86 stage 4-0's taint prototype (scripts/taintstats.sh, a build with -DTTTT_TAINT; one checker on one thread).
// Which lazily computed results are *clean*, and so could be published to a graph other checkers share: their
// computation popped no cycle, read nothing in flight and no tainted result, moved no limit counter, reported nothing
// at the current node, and ended outside flow_uncached and in the typecheck phase. A thread-local counter that every
// tainting event bumps; a computation records it when it starts and compares when it ends, so a tainted inner result
// taints what is around it. Results of the intern tables are found automatically (a lookup that missed, then the key
// put: the window between; a key put first, then its value: until the value is there), the members, property types,
// declaration memos and the other LazyStates explicitly. Nothing changes what the checker computes.
// Cleandiff: every clean result of a few kinds is fingerprinted structurally (types by what they are, ids replaced by
// the referents' fingerprints), and two checkers that compute the same key must agree (`--checker-memory 0` gives a
// fresh checker, so a history of its own, to every file)

#include <stdint.h>

typedef enum TaintReason {
    TAINT_CYCLE,            // ty_resolving_cycle() & co: a cycle was found, what is on the stack since its start is part of it
    TAINT_IN_FLIGHT_LAZY,   // a declaration's LazyType asked for while in flight
    TAINT_IN_FLIGHT_MEMBERS,
    TAINT_IN_FLIGHT_PROP,
    TAINT_IN_FLIGHT_SLOT,   // an intern table entry whose value is being computed (a placeholder, a null slot)
    TAINT_IN_FLIGHT_OTHER,  // any other LazyState or in-flight marker read while in flight
    TAINT_LIMIT,            // instantiation_limit_hits / relation_*_hits moved, a constraint depth ran out
    TAINT_CURRENT_NODE,     // reported at the current node (ty_error_at_current_node())
    TAINT_READ_TAINTED,     // read a result whose own computation was tainted
    TAINT_END_CONDITION,    // ended under flow_uncached or outside the typecheck phase
    TAINT_PEEK,             // answered differently because something isn't resolved yet (a done-ness peek)
    TAINT_REASON_COUNT,
} TaintReason;

// The explicitly instrumented kinds (the KEY_* kinds of the intern tables are below 160)
enum {
    TK_MEMBERS = 160,       // ty_members() of an object type, a reference, an intersection
    TK_DECLARED_RAW,        // declared_raw_members()
    TK_PROP_TYPE,           // prop_type_slow()
    TK_SIGNATURE_RETURN,    // signature_return_type()'s resolution
    TK_PARAM_CONSTRAINT,    // a type parameter's constraint
    TK_SYMBOL_DECLARED,     // SymbolLinks.declared_state (a declared type)
    TK_FUNCTION_RETURN,     // an inferred return type (KEY_FUNCTION_RETURN)
    TK_BASE_CONSTRAINT,     // resolved_base_constraint() (KEY_BASE_CONSTRAINT)
    TK_SIMPLIFIED,          // ty_simplified() (KEY_SIMPLIFIED)
    TK_VARIANCES,           // get_variances() (KEY_VARIANCES)
    TK_FREE_PARAMS,         // ty_function_free_params() (KEY_FUNCTION_FREE_PARAMS, a node_links kind)
    TK_LAZY_DECL = 176,     // + LazyType tag: a declaration's LazyType (not expressions')
    TK_LAZY_EXPR = 184,     // an expression's LazyType
    TK_COUNT = 256,
};

#ifdef TTTT_TAINT

typedef struct TaintFrame {
    uint64_t taint;
    uint64_t start;
    uint64_t reports;
    uint64_t attributed;
} TaintFrame;

void taint_event(TaintReason reason);
void taint_event_at(TaintReason reason, char const* file, int line); // taint_event(), counted by site
TaintFrame taint_begin(void);
// `object` identifies the result (what taint_read() is asked with), `subject` and `result` what cleandiff fingerprints
// (by kind)
void taint_end(TaintFrame frame, unsigned kind, void const* object, void const* subject, void const* result);
void taint_read(unsigned kind, void const* object);
// An explicitly instrumented intern entry: its in-flight placeholder is hit
void taint_slot_begin(void const* const* block);
void taint_slot_end(void const* const* block);
// Intern table hooks (checker.c)
void taint_intern_miss(void const* table, uint64_t hash);
void taint_intern_put(void const* table, uint64_t hash, void const** block, unsigned kind);
void taint_intern_hit(void const* const* block);
void taint_report(void); // ty_error_at()
void taint_checker_new(void* checker);
void taint_checker_drop(void* checker);
void taint_print(void);

void taint_flow_change(void); // flow_uncached is about to change: what is done by now ends under its current value

#define TAINT_EVENT(reason) taint_event_at(reason, __FILE__, __LINE__)
#define TAINT_FLOW() taint_flow_change()
// `condition`, a taint event when it holds
#define TAINT_IF(reason, condition) ((condition) ? (taint_event_at(reason, __FILE__, __LINE__), true) : false)
// TTTT_TAINT_LENIENT=1 doesn't count the done-ness peeks (TAINT_PEEK): cleandiff then says whether they matter
#define TAINT_BEGIN(name) TaintFrame name = taint_begin()
#define TAINT_END(name, kind, object, subject, result) taint_end(name, kind, object, subject, result)
#define TAINT_READ(kind, object) taint_read(kind, object)

#else

// --shared-graph's cheap taint (plan 86 stage 4 track S, ty/graph.c): the G0 rule's events (TTTT_TAINT_LENIENT's:
// done-ness peeks don't count) bump a counter, a computation compares it at its end, and what ended tainted is marked
// so that reading it is an event too. Always on and cheap: an increment per event (rare: cycles, in-flight reads,
// limits), a load at each frame's start and end, and a test of graph_taint_marks where tracked results are read.
// Without --shared-graph nothing is ever marked or pending
extern thread_local uint64_t graph_taint_now;
extern thread_local uint32_t graph_taint_pending; // intern entries put before their value (graph.c)
extern bool graph_taint_marks; // Anything marked tainted (any checker)
void graph_taint_sweep(void);
void graph_taint_end(unsigned kind, void const* object, void const* subject);
void graph_taint_read(unsigned kind, void const* object);
static inline void graph_taint_tick(void) {
    graph_taint_now++;
    if (graph_taint_pending) graph_taint_sweep();
}
typedef struct TaintFrame { uint64_t taint; } TaintFrame;

#define TAINT_EVENT(reason) graph_taint_tick()
#define TAINT_FLOW() ((void)0)
// (The lenient rule: a peek is no event)
#define TAINT_IF(reason, condition) ((reason) == TAINT_PEEK ? (condition) : (condition) ? (graph_taint_tick(), true) : false)
#define TAINT_BEGIN(name) TaintFrame name = { graph_taint_now }
#define TAINT_END(name, kind, object, subject, result) \
    (graph_taint_now != (name).taint ? graph_taint_end(kind, object, subject) : (void)0)
#define TAINT_READ(kind, object) (graph_taint_marks ? graph_taint_read(kind, object) : (void)0)

#endif
