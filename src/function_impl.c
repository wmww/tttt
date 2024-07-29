#include "function_impl.h"
#include "name_stack.h"
#include "callable_type.h"
#include "generic.h"
#include "statement.h"
#include "error.h"

void function_impl_drop(FunctionImpl* me) {
    callable_type_drop(&my callable);
    named_variable_destroy(my named_var);
    vec_expr_drop(&my arg_defaults);
    block_drop(&my body);
}

void cstr_append_function_impl_debug(cstr* target, FunctionImpl* me, bool include_arrow, int indent) {
    cstr_append_generic_debug(target, &my callable.generic, indent);
    cstr_append_sv(target, c_sv("("));
    c_foreach(i, vec_callable_arg, my callable.args) {
        if (i.ref->rest) {
            cstr_append_sv(target, c_sv("..."));
        }
        cstr_append_binding_debug(target, &i.ref->binding, i.ref->explicit_optional, indent);
        intptr_t index = i.ref - my callable.args.data;
        Expr const* default_arg = function_impl_arg_default_at(me, index);
        if (default_arg->tag != EXPR_IMPLICIT_ANY) {
            cstr_append_sv(target, c_sv(" = "));
            cstr_append_expr_debug(target, default_arg, indent);
        }
        if (i.ref != i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(")"));
    if (my callable.ret_explicit) {
        cstr_append_sv(target, c_sv(": "));
        cstr_append_type_debug(target, &my callable.ret, indent);
    }
    if (my has_block) {
        cstr_append_sv(target, include_arrow ? c_sv(" => ") : c_sv(" "));
        cstr_append_block_debug(target, &my body, indent);
    } else {
        cstr_append_sv(target, c_sv(";"));
    }
}

void function_impl_add_names(FunctionImpl* me, NameStack* names, ErrorCtx* errs) {
    ASSERT(!my named_var);
    my named_var = make_named_variable(my full_span, lazy_type_function(me), &my callable.generic);
    if (!name_stack_add_variable_s(names, cstr_from_span(&my name), my named_var)) {
        error_ctx_error(errs, my name, TS_CODE_NONE, "multiple values declared with same name");
    }
}

void function_impl_resolve_names(FunctionImpl* me, NameStack* names, ErrorCtx* errs) {
    callable_type_resolve_names(&my callable, me, names, errs);
}

Type const* function_impl_resolve_type(FunctionImpl* me) {
    if (my callable.ret_explicit) {
        my ret_resolve_state = RESOLVE_STATE_DONE;
    }

    switch (my ret_resolve_state) {
        case RESOLVE_STATE_UNINITIALIZED:
            type_drop(&my callable.ret);
            my ret_resolve_state = RESOLVE_STATE_IN_FLIGHT;

            StatementCtx ctx = statement_ctx_function(&type_any, true);
            block_typecheck(&my body, &ctx, nullptr);

            if (my ret_resolve_state == RESOLVE_STATE_IN_FLIGHT) {
                if (ctx.impl_return.tag == TYPE_NEVER) {
                    // This will need to be improved
                    my callable.ret = type_void;
                } else {
                    my callable.ret = type_move(&ctx.impl_return);
                    type_abstract_literals(&my callable.ret);
                }
                my ret_resolve_state = RESOLVE_STATE_DONE;
            }

            statement_ctx_drop(&ctx);

            break;

        case RESOLVE_STATE_IN_FLIGHT:
            type_drop(&my callable.ret);
            my callable.ret = type_self_referential_error;
            my ret_resolve_state = RESOLVE_STATE_DONE;
            break;

        case RESOLVE_STATE_DONE: break;
    }

    return callable_type_get_wrapper(&my callable);
}

void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs) {
    function_impl_resolve_type(me);
    callable_type_typecheck(&my callable, me, errs);

    if (my newline_before_arrow) {
        error_ctx_error(errs, my arrow_span, 1200, "Line terminator not permitted before arrow.");
    }

    StatementCtx ctx = statement_ctx_function(my callable.ret_explicit ? &my callable.ret : &type_any, false);
    block_typecheck(&my body, &ctx, errs);
    statement_ctx_drop(&ctx);
}
