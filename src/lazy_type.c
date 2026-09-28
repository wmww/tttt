#include "lazy_type.h"
#include "display.h"
#include "several_types.h"
#include "expr.h"
#include "statement.h"
#include "function_impl.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "interface_decl.h"
#include "scope.h"
#include "ty/ty.h"

thread_local LazySlots* current_lazy_slots;

u32 lazy_slot_new(void) {
    static _Atomic u32 next = 1;
    u32 ret = next++;
    panic_if(ret >> (LAZY_PAGE_BITS + LAZY_DIR_BITS + LAZY_TOP_BITS), "out of LazyType slots");
    return ret;
}

static LazyMemo* lazy_type_state(LazyType* me) {
    panic_if(!my slot, "LazyType with tag ", (i32)my tag, " was never given a slot");
    panic_if(!current_lazy_slots, "LazyType evaluated without a current checker");
    LazyMemo*** dir = &current_lazy_slots->top[my slot >> (LAZY_PAGE_BITS + LAZY_DIR_BITS)];
    if (!*dir) {
        *dir = zeroed_table(1 << LAZY_DIR_BITS, sizeof(LazyMemo*));
        panic_if(!*dir, "out of memory");
    }
    LazyMemo** page = &(*dir)[(my slot >> LAZY_PAGE_BITS) & ((1 << LAZY_DIR_BITS) - 1)];
    if (!*page) {
        *page = zeroed_table(1 << LAZY_PAGE_BITS, sizeof(LazyMemo));
        panic_if(!*page, "out of memory");
    }
    return &(*page)[my slot & ((1 << LAZY_PAGE_BITS) - 1)];
}

void lazy_slots_drop(LazySlots* me) {
    for (size_t i = 0; i < c_arraylen(my top); i++) {
        if (!my top[i]) continue;
        for (size_t j = 0; j < (1 << LAZY_DIR_BITS); j++) {
            LazyMemo* page = my top[i][j];
            if (!page) continue;
            free(page);
        }
        free(my top[i]);
        my top[i] = nullptr;
    }
}

void lazy_type_drop(LazyType* me) {
    if (my slot) {
        // Dropped while a checker is around: this LazyType was made during typechecking, and nothing will ask for its
        // slot again. Slots of the AST die with their checker instead, which is gone before the AST is
        LazyMemo* state = lazy_type_state_peek(me);
        if (state) *state = (LazyMemo){0};
    }
}

void lazy_type_set(LazyType* me, Ty const* result, Ty const* context) {
    LazyMemo* state = lazy_type_state(me);
    state->evaluated = result;
    state->context = context;
    state->evaluation_started = true;
    state->done = true;
}

// TTTT_DEBUG_CYCLE=1: when a declaration's evaluation reaches itself again (the TS7022 kind of cycle), print the
// LazyTypes on the way, innermost first. Only LazyTypes: what is evaluated without one in between isn't shown
typedef struct CycleTrace {
    LazyType* stack[4096];
    u32 len;
} CycleTrace;
static thread_local CycleTrace cycle_trace;

static void cycle_trace_print(LazyType* lazy) {
    switch (lazy->tag) {
        case LAZY_TYPE_EXPR: {
            Span span = c_container_of(lazy, Expr, lazy_type)->outer_span;
            log_warn("  expression, line ", span_line_index(span) + 1, ": ", span);
        }   break;
        case LAZY_TYPE_INITIALIZED_BINDING: {
            Span span = c_container_of(lazy, InitializedBinding, lazy_type)->binding.content_span;
            log_warn("  binding, line ", span_line_index(span) + 1, ": ", span);
        }   break;
        case LAZY_TYPE_FUNCTION:
            log_warn("  function, line ", span_line_index(c_container_of(lazy, FunctionImpl, lazy_type)->full_span) + 1);
            break;
        default:
            log_warn("  LazyType tag ", (i32)lazy->tag);
            break;
    }
}

static Ty const* lazy_type_evaluate_inner(LazyType* me);

Ty const* lazy_type_evaluate(LazyType* me) {
    if (!debug_env.debug_cycle || cycle_trace.len == c_arraylen(cycle_trace.stack)) return lazy_type_evaluate_inner(me);
    LazyMemo* state = my tag == LAZY_TYPE_EVALUATED ? nullptr : lazy_type_state(me);
    if (state && state->evaluation_started && !state->done && my tag != LAZY_TYPE_EXPR && !state->is_self_referential) {
        log_warn("cycle:");
        cycle_trace_print(me);
        for (u32 i = cycle_trace.len; i--;) {
            cycle_trace_print(cycle_trace.stack[i]);
            if (cycle_trace.stack[i] == me) break;
        }
    }
    cycle_trace.stack[cycle_trace.len++] = me;
    Ty const* ret = lazy_type_evaluate_inner(me);
    cycle_trace.len--;
    return ret;
}

static Ty const* lazy_type_evaluate_inner(LazyType* me) {
    if (my tag == LAZY_TYPE_EVALUATED) return ty_intrinsic(current_checker, TY_ANY);

    // THREADSAFETY: slots belong to one checker, which belongs to one thread
    LazyMemo* state = lazy_type_state(me);

    if (state->evaluation_started) {
        // We've already hit this LazyType once, therefore it's self-referential
        // Circularity is a property of declarations (that is where tsc detects and reports it). An expression on the
        // way only has to stop a runaway: which of the two is hit first depends on who asks first, and the answer
        // shouldn't (control flow analysis of a loop re-enters the expressions of the loop body through declarations)
        if (my tag == LAZY_TYPE_EXPR && state->hits < 8) {
            state->hits++;
        } else if (!state->is_self_referential) {
            state->is_self_referential = true;
        } else {
            // We have hit this LazyType 3 times, which means we've hit every other LazyType in this cycle twice, which
            // means we have hit every other LazyType at least twice, which means we've marked them all self-referential
            // and so we can now safely bail.
            state->done = true;
            state->evaluated = ty_intrinsic(current_checker, TY_CIRCULAR);
            return state->evaluated;
        }
    }
    state->evaluation_started = true;

    // Declarations evaluate self-contained, see ty_flow_isolate()
    bool isolate = my tag != LAZY_TYPE_EXPR;
    FlowIsolation isolation = {0};
    if (isolate) isolation = ty_flow_isolate(current_checker);

    Ty const* result = nullptr;
    switch (my tag) {
        case LAZY_TYPE_UNINITIALIZED: panic("LazyType is uninitialized");
        case LAZY_TYPE_EVALUATED: panic("lazy_type_evaluate() called when already evaluated");

        case LAZY_TYPE_EXPR:
            result = expr_evaluate_type(c_container_of(me, Expr, lazy_type));
            break;

        case LAZY_TYPE_INITIALIZED_BINDING:
            result = initialized_binding_evaluate_assigned_type(c_container_of(me, InitializedBinding, lazy_type));
            break;

        case LAZY_TYPE_FOR_EACH_LOOP_VAR:
            result = for_each_loop_statement_evaluate_var_type(c_container_of(me, ForEachLoopStatement, var_type));
            break;

        case LAZY_TYPE_FUNCTION:
            result = function_impl_evaluate_type(c_container_of(me, FunctionImpl, lazy_type));
            break;

        case LAZY_TYPE_CLASS_ITEM_FIELD:
            result = class_item_field_evaluate_type(c_container_of(me, ClassItem, field.lazy_type));
            break;

        case LAZY_TYPE_CLASS_ITEM_ACCESSOR:
            result = class_item_accessor_evaluate_type(c_container_of(me, ClassItem, method.accessor_type));
            break;

    }
    if (isolate) ty_flow_restore(current_checker, isolation);

    if (!state->done) {
        // The calls this function made didn't decide that we're self-referential, result is valid
        state->evaluated = result;
        state->done = true;
    }
    return state->evaluated;
}
