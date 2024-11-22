#include "lazy_type.h"
#include "several_types.h"
#include "expr.h"
#include "statement.h"
#include "function_impl.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "interface_decl.h"

void lazy_type_evaluate(LazyType* me) {
    // THREADSAFETY: this will probably need to be rewritten to be threadsafe

    if (my evaluation_started) {
        // We've already hit this LazyType once, therefore it's self-referential
        if (my is_self_referential) {
            // We have hit this LazyType 3 times, which means we've hit every other LazyType in this cycle twice, which
            // means we have hit every other LazyType at least twice, which means we've marked them all self-referential
            // and so we can now safely bail.
            my tag = LAZY_TYPE_EVALUATED;
            my evaluated = type_self_referential_error;
            return;
        }
        my is_self_referential = true;
    }
    my evaluation_started = true;

    Type result = {0};
    switch (my tag) {
        case LAZY_TYPE_UNINITIALIZED: PANIC("LazyType is uninitialized");
        case LAZY_TYPE_EVALUATED: PANIC("lazy_type_evaluate() called when already evaluated");

        case LAZY_TYPE_EXPR:
            result = expr_evaluate_type(c_container_of(me, Expr, lazy_type));
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

        case LAZY_TYPE_ENUM_INSTANCE:
            result = enum_decl_evaluate_instance_type(c_container_of(me, EnumDecl, instance_type));
            break;

        case LAZY_TYPE_ENUM_INTERFACE:
            result = enum_decl_evaluate_interface_type(c_container_of(me, EnumDecl, interface_type));
            break;

        case LAZY_TYPE_INTERFACE_DECL:
            result = interface_decl_evaluate_type(c_container_of(me, InterfaceDecl, lazy_type));
            break;

        case LAZY_TYPE_IDENTIFIER_TYPE:
            result = identifier_type_evaluate_type(c_container_of(me, IdentifierType, type));
            break;

        case LAZY_TYPE_DECLARATION:
            result = declaration_evaluate_declared_type(c_container_of(me, Declaration, declared_type_lazy));
            break;
    }

    if (my tag != LAZY_TYPE_EVALUATED) {
        // Our tag wasn't touched by the call this function made, result is valid
        my evaluated = type_clone(*type_resolve_until_identifier(&result));
        my tag = LAZY_TYPE_EVALUATED;
    }

    type_drop(&result);
}
