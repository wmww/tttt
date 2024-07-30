#include "lazy_type.h"
#include "expr.h"
#include "statement.h"
#include "function_impl.h"
#include "class_decl.h"
#include "type_decl.h"

LazyType lazy_type_interface(TypeDecl* interface) {
    ASSERT(interface->is_interface);
    return (LazyType){
        .tag = LAZY_TYPE_INTERFACE,
        .interface = interface,
    };
}

LazyType lazy_type_indexed(ArcLazyType source, Value index) {
    return (LazyType){
        .tag = LAZY_TYPE_INDEXED,
        .indexed = {
            .source = source,
            .index = c_new(Type, type_value(index)),
        }
    };
}

void lazy_type_drop(LazyType* me) {
    switch (my tag) {
        case LAZY_TYPE_VALUE:
            type_drop(&my value);
            break;

        case LAZY_TYPE_INDEXED:
            arc_lazy_type_drop(&my indexed.source);
            type_drop(my indexed.index);
            free(my indexed.index);
            break;

        case LAZY_TYPE_REF:
        case LAZY_TYPE_IN_FLIGHT:
        case LAZY_TYPE_EXPR:
        case LAZY_TYPE_FOR_EACH_LOOP_VAR:
        case LAZY_TYPE_FUNCTION:
        case LAZY_TYPE_CLASS_INSTANCE:
        case LAZY_TYPE_CLASS_CLASS:
        case LAZY_TYPE_INTERFACE:
            break;
    }
}

LazyType lazy_type_clone(LazyType me) {
    switch (me.tag) {
        case LAZY_TYPE_VALUE:
            me.value = type_clone(me.value);
            break;

        case LAZY_TYPE_INDEXED:
            me.indexed.source = arc_lazy_type_clone(me.indexed.source);
            me.indexed.index = c_new(Type, type_clone(*me.indexed.index));
            break;

        case LAZY_TYPE_REF:
        case LAZY_TYPE_IN_FLIGHT:
        case LAZY_TYPE_EXPR:
        case LAZY_TYPE_FOR_EACH_LOOP_VAR:
        case LAZY_TYPE_FUNCTION:
        case LAZY_TYPE_CLASS_INSTANCE:
        case LAZY_TYPE_CLASS_CLASS:
        case LAZY_TYPE_INTERFACE:
            break;
    }
    return me;
}

void cstr_append_lazy_type_debug(cstr* target, LazyType const* me, int indent) {
    Type const* t = lazy_type_try_get(me);
    if (t) {
        cstr_append_type_debug(target, t, indent);
    } else {
        cstr_append_sv(target, c_sv(CLR_TYPE "/* unevaluated */" CLR0));
    }
}

void cstr_append_lazy_type_error_msg(cstr* target, LazyType const* me) {
    Type const* t = lazy_type_try_get(me);
    if (t) {
        cstr_append_type_error_msg(target, t);
    } else {
        cstr_append_sv(target, c_sv(CLR_TYPE "/* unevaluated */" CLR0));
    }
}

Type const* lazy_type_resolve(LazyType* me) {
    switch (my tag) {
        case LAZY_TYPE_VALUE:
        case LAZY_TYPE_REF:
            break;

        case LAZY_TYPE_IN_FLIGHT:
            *me = lazy_type_value(type_self_referential_error);
            break;

        case LAZY_TYPE_EXPR: {
            my tag = LAZY_TYPE_IN_FLIGHT;

            bool abstract_literals = my expr.abstract_literals;
            Type resolved = expr_get_type_and_typecheck(my expr.ptr, nullptr);

            if (my tag == LAZY_TYPE_IN_FLIGHT) {
                if (abstract_literals) {
                    type_abstract_literals(&resolved);
                }
                type_abstract_tuple_to_array(&resolved);
                if (resolved.tag == TYPE_ARRAY &&
                    resolved.array.element->tag == TYPE_VALUE &&
                    resolved.array.element->value.tag == VALUE_UNDEFINED
                ) {
                    type_drop(resolved.array.element);
                    *resolved.array.element = type_any;
                }
                *me = lazy_type_value(resolved);
            } else {
                type_drop(&resolved);
            }
        }   break;

        case LAZY_TYPE_FOR_EACH_LOOP_VAR: {
            my tag = LAZY_TYPE_IN_FLIGHT;
            Type const* resolved = for_each_loop_statement_resolve_var_type(my for_each_loop_var);
            if (my tag == LAZY_TYPE_IN_FLIGHT) {
                *me = lazy_type_ref(resolved);
            }
        }   break;

        case LAZY_TYPE_FUNCTION: {
            my tag = LAZY_TYPE_IN_FLIGHT;
            Type const* resolved = function_impl_resolve_type(my function);
            if (my tag == LAZY_TYPE_IN_FLIGHT) {
                *me = lazy_type_ref(resolved);
            }
        }   break;

        case LAZY_TYPE_CLASS_INSTANCE:
        case LAZY_TYPE_CLASS_CLASS: {
            my tag = LAZY_TYPE_IN_FLIGHT;
            Type const* resolved = class_decl_resolve_type(my class, my tag == LAZY_TYPE_CLASS_INSTANCE);
            if (my tag == LAZY_TYPE_IN_FLIGHT) {
                *me = lazy_type_ref(resolved);
            }
        }   break;

        case LAZY_TYPE_INTERFACE: {
            my tag = LAZY_TYPE_IN_FLIGHT;
            Type resolved = type_decl_make_interface_type(my interface);
            if (my tag == LAZY_TYPE_IN_FLIGHT) {
                *me = lazy_type_value(resolved);
            } else {
                type_drop(&resolved);
            }
        }   break;

        case LAZY_TYPE_INDEXED: {
            LazyType local_me = *me;
            my tag = LAZY_TYPE_IN_FLIGHT;
            Type const* source_resolved = lazy_type_resolve(my indexed.source.get);
            if (my tag == LAZY_TYPE_IN_FLIGHT) {
                Type result = index_type_of(source_resolved, my indexed.index, span_null, nullptr);
                if (my tag == LAZY_TYPE_IN_FLIGHT) {
                    *me = lazy_type_value(result);
                } else {
                    type_drop(&result);
                }
            }
            lazy_type_drop(&local_me);
        }   break;
    }
    return lazy_type_assert_get(me);
}

void lazy_type_typecheck(LazyType* me, ErrorCtx* errs) {
    lazy_type_resolve(me);
    switch (my tag) {
        case LAZY_TYPE_VALUE: type_typecheck(&my value, errs); break;
        case LAZY_TYPE_REF: break;
        default: PANIC("LazyType failed to resolve with tag %d", my tag);
    }
}
