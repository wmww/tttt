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

// The thread's block of slots, a memo page's worth: the parse threads would interleave their files' slots one by one
// otherwise, and every page a checker touches would be shared with files other checkers check (vscode at 4 checkers:
// a third to a half of each checker's pages used)
static thread_local u32 slot_next, slot_end;

u32 lazy_slot_new(void) {
    if (slot_next == slot_end) {
        static _Atomic u32 next_block = 1; // (Block 0 holds slot 0, which isn't one)
        u32 block = next_block++;
        panic_if(block >> (LAZY_DIR_BITS + LAZY_TOP_BITS), "out of LazyType slots");
        slot_next = block << LAZY_PAGE_BITS;
        slot_end = slot_next + (1 << LAZY_PAGE_BITS);
    }
    return slot_next++;
}

void lazy_slots_new_block(void) {
    if (slot_next & ((1 << LAZY_PAGE_BITS) - 1)) slot_next = slot_end;
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

void lazy_slots_add_type(LazySlots* me, Ty const* ty) {
    Ty const*** chunk = &my types[ty->id >> LAZY_TYPE_CHUNK_BITS];
    if (!*chunk) {
        *chunk = malloc(sizeof(Ty const*) << LAZY_TYPE_CHUNK_BITS);
        panic_if(!*chunk, "out of memory");
    }
    (*chunk)[ty->id & ((1 << LAZY_TYPE_CHUNK_BITS) - 1)] = ty;
}

static u32 lazy_type_id(Ty const* ty) {
    return ty ? ty->id : 0;
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
    for (size_t i = 0; i < c_arraylen(my types); i++) {
        free(my types[i]);
        my types[i] = nullptr;
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
    state->evaluated = lazy_type_id(result);
    state->context = lazy_type_id(context);
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
            Span span = expr_outer_span(c_container_of(lazy, Expr, lazy_type));
            log_warn("  expression, line ", span_line_index(span) + 1, ": ", span);
        }   break;
        case LAZY_TYPE_INITIALIZED_BINDING: {
            Span span = c_container_of(lazy, InitializedBinding, lazy_type)->binding.content_span;
            log_warn("  binding, line ", span_line_index(span) + 1, ": ", span);
        }   break;
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

// A destructuring parameter or variable: tsc gives it no symbol of its own to push, its names are what's circular
static Binding const* lazy_type_pattern_root(LazyType const* me) {
    if (my tag != LAZY_TYPE_INITIALIZED_BINDING) return nullptr;
    Binding const* binding = &c_container_of(me, InitializedBinding, lazy_type)->binding;
    return binding->tag == BINDING_ARRAY || binding->tag == BINDING_OBJECT ? binding : nullptr;
}

static Ty const* lazy_type_evaluate_inner(LazyType* me) {
    if (my tag == LAZY_TYPE_EVALUATED) return ty_intrinsic(current_checker, TY_ANY);

    // THREADSAFETY: slots belong to one checker, which belongs to one thread
    LazyMemo* state = lazy_type_state(me);

    if (state->evaluation_started) {
        TAINT_EVENT(TAINT_IN_FLIGHT_LAZY);
        // Circularity is a property of declarations (that is where tsc detects and reports it): asked for again while in
        // flight, a declaration is any, and it and everything resolved since is part of the cycle (tsc's
        // pushTypeResolution failing, see ty_resolving_push()). What they reach finds out when it finishes.
        // In flight only below the barrier of a call being resolved (tsc's findResolutionCycleStartIndex from
        // resolutionStart), it isn't a cycle: it's evaluated again, once, and the inner evaluation's result is the one
        // remembered (tsc's getTypeOfVariableOrParameterOrProperty)
        bool below_barrier = my tag != LAZY_TYPE_EXPR && !lazy_type_pattern_root(me) &&
            !ty_resolving_find(current_checker, RESOLVING_DECLARATION, me);
        if (below_barrier) {
            if (state->reevaluating) goto circular;
            state->reevaluating = true;
            goto evaluate;
        }
        circular:
        if (my tag != LAZY_TYPE_EXPR) {
            Binding const* pattern = lazy_type_pattern_root(me);
            if (pattern) ty_resolving_pattern_cycle(current_checker, me, pattern);
            else ty_resolving_cycle(current_checker, RESOLVING_DECLARATION, me);
            return ty_intrinsic(current_checker, TY_CIRCULAR);
        }
        // An expression on the way only has to stop a runaway: which of the two is hit first depends on who asks
        // first, and the answer shouldn't (control flow analysis of a loop re-enters the expressions of the loop body
        // through declarations)
        if (my tag == LAZY_TYPE_EXPR && state->hits < 8) {
            state->hits++;
        } else if (!state->is_self_referential) {
            state->is_self_referential = true;
        } else {
            // We have hit this LazyType 3 times, which means we've hit every other LazyType in this cycle twice, which
            // means we have hit every other LazyType at least twice, which means we've marked them all self-referential
            // and so we can now safely bail.
            state->done = true;
            state->evaluated = ty_intrinsic(current_checker, TY_CIRCULAR)->id;
            return ty_intrinsic(current_checker, TY_CIRCULAR);
        }
    }
    state->evaluation_started = true;
    evaluate:;

    // Declarations evaluate self-contained, see ty_flow_isolate(). (A variable's initializer only around its full check,
    // initialized_binding_evaluate_assigned_type(): tsc's getQuickTypeOfExpression comes first, outside
    // checkExpressionCached, and sees the loops in progress)
    bool isolate = my tag != LAZY_TYPE_EXPR && my tag != LAZY_TYPE_INITIALIZED_BINDING;
    FlowIsolation isolation = {0};
    if (isolate) isolation = ty_flow_isolate(current_checker);

    bool resolving = my tag != LAZY_TYPE_EXPR;
#ifdef TTTT_TAINT
    TaintFrame taint_frame = taint_begin();
    unsigned taint_kind = my tag == LAZY_TYPE_EXPR ? TK_LAZY_EXPR : TK_LAZY_DECL + my tag;
#endif
    if (resolving) ty_resolving_push(current_checker, lazy_type_pattern_root(me) ? RESOLVING_PATTERN : RESOLVING_DECLARATION, me);
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

        case LAZY_TYPE_CLASS_ITEM_FIELD:
            result = class_item_field_evaluate_type(c_container_of(me, ClassItem, field.lazy_type));
            break;

        case LAZY_TYPE_CLASS_ITEM_ACCESSOR:
            result = class_item_accessor_evaluate_type(c_container_of(me, ClassItem, method.accessor_type));
            break;

    }
    if (isolate) ty_flow_restore(current_checker, isolation);
    if (resolving && ty_resolving_pop(current_checker)) {
        // A pattern's type isn't memoized by tsc (getTypeForBindingElementParent): the names that made it circular
        // are, and so is the initializer, so asked again later it is what the initializer evaluated to (`var { a: { b }
        // } = { a: { b } }` destructures `{ a: { b: any } }`). Evaluated again once at most
        if (lazy_type_pattern_root(me) && !state->pattern_retried) {
            state->pattern_retried = true;
            state->evaluation_started = false;
            TAINT_EVENT(TAINT_CYCLE);
            return ty_intrinsic(current_checker, TY_CIRCULAR);
        }
        // Part of a cycle: any, and the declaration reports it (tsc's reportCircularityError), whatever it evaluated to
        state->evaluated = ty_intrinsic(current_checker, TY_CIRCULAR)->id;
        state->done = true;
    }

    if (!state->done) {
        // The calls this function made didn't decide that we're self-referential, result is valid. A declaration that
        // only reads one that is circular (`var b = a` after `var a = a`) is plain any: the circular one reported
        // it (tsc's getTypeOfSymbol of it is anyType)
        if (resolving && result == ty_intrinsic(current_checker, TY_CIRCULAR) && !lazy_type_pattern_root(me)) {
            result = ty_intrinsic(current_checker, TY_ANY);
        }
        state->evaluated = lazy_type_id(result);
        state->done = true;
    }
#ifdef TTTT_TAINT
    taint_end(taint_frame, taint_kind, me, me, lazy_memo_evaluated(state));
#endif
    return lazy_memo_evaluated(state);
}
