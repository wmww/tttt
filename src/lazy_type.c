#include "lazy_type.h"
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
        *dir = calloc(1 << LAZY_DIR_BITS, sizeof(LazyMemo*));
        panic_if(!*dir, "out of memory");
    }
    LazyMemo** page = &(*dir)[(my slot >> LAZY_PAGE_BITS) & ((1 << LAZY_DIR_BITS) - 1)];
    if (!*page) {
        *page = calloc(1 << LAZY_PAGE_BITS, sizeof(LazyMemo));
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
    if (my tag == LAZY_TYPE_EVALUATED) {
        type_drop(&my preevaluated);
    } else if (my slot) {
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

Ty const* lazy_type_evaluate(LazyType* me) {
    if (my tag == LAZY_TYPE_EVALUATED) return ty_from_node(current_checker, &my preevaluated, (LowerCtx){ .generic = true });

    // THREADSAFETY: slots belong to one checker, which belongs to one thread
    LazyMemo* state = lazy_type_state(me);

    if (state->evaluation_started) {
        // We've already hit this LazyType once, therefore it's self-referential
        if (state->is_self_referential) {
            // We have hit this LazyType 3 times, which means we've hit every other LazyType in this cycle twice, which
            // means we have hit every other LazyType at least twice, which means we've marked them all self-referential
            // and so we can now safely bail.
            state->done = true;
            state->evaluated = ty_intrinsic(current_checker, TY_ERROR);
            return state->evaluated;
        }
        state->is_self_referential = true;
    }
    state->evaluation_started = true;

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

        case LAZY_TYPE_CLASS_INSTANCE:
            result = class_decl_evaluate_type(c_container_of(me, ClassDecl, instance_type), true);
            break;

        case LAZY_TYPE_CLASS_CLASS:
            result = class_decl_evaluate_type(c_container_of(me, ClassDecl, class_type), false);
            break;

        case LAZY_TYPE_CLASS_ITEM_FIELD:
            result = class_item_field_evaluate_type(c_container_of(me, ClassItem, field.lazy_type));
            break;

        case LAZY_TYPE_CLASS_ITEM_ACCESSOR:
            result = class_item_accessor_evaluate_type(c_container_of(me, ClassItem, method.accessor_type));
            break;

        case LAZY_TYPE_ENUM_INSTANCE:
            result = enum_decl_evaluate_instance_type(c_container_of(me, EnumDecl, instance_type));
            break;

        case LAZY_TYPE_ENUM_INTERFACE:
            result = enum_decl_evaluate_interface_type(c_container_of(me, EnumDecl, interface_type));
            break;

        case LAZY_TYPE_INTERFACE_DECL:
            result = interface_decl_evaluate_type(c_container_of(me, InterfaceDecl, lazy_type));
            break;

        case LAZY_TYPE_MERGED:
            result = merged_type_evaluate_type(c_container_of(me, MergedType, lazy));
            break;

        case LAZY_TYPE_SCOPE_VALUE_INTERFACE:
            result = symbol_evaluate_value_interface(c_container_of(me, Symbol, value_interface));
            break;
    }

    if (!state->done) {
        // The calls this function made didn't decide that we're self-referential, result is valid
        state->evaluated = result;
        state->done = true;
    }
    return state->evaluated;
}
