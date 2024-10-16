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

    typeof(my tag) initial_tag = my tag;
    my tag = LAZY_TYPE_IN_FLIGHT;

    Type result = {0};
    switch (initial_tag) {
        case LAZY_TYPE_UNINITIALIZED: PANIC("LazyType is uninitialized");
        case LAZY_TYPE_EVALUATED: PANIC("lazy_type_evaluate() called when already evaluated");

        case LAZY_TYPE_IN_FLIGHT:
            result = type_self_referential_error;
            break;

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

    if (initial_tag != LAZY_TYPE_IDENTIFIER_TYPE) {
        // TODO: this seems wrong, is it needed once chains of multiple self-referential LazyTypes are handled correctly?
        type_resolve(&result); // Ignore return value, do this so we'll pick up on self-referential types
    }

    switch (my tag) {
        case LAZY_TYPE_IN_FLIGHT:
            // Our tag wasn't touched by the call this function made, result is valid
            my evaluated = result;
            my tag = LAZY_TYPE_EVALUATED;
            break;

        case LAZY_TYPE_EVALUATED:
            // A subcall set our evaluated type, don't mess with it
            if (initial_tag == LAZY_TYPE_FUNCTION) {
                type_drop(&my evaluated);
                my evaluated = result;
            } else {
                type_drop(&result);
            }
            break;

        default: PANIC("LazyType tag changed to something other than LAZY_TYPE_EVALUATED during evaluation");\
    }
}
