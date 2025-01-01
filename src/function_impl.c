#include "function_impl.h"
#include "scope_stack.h"
#include "callable_type.h"
#include "generic.h"
#include "statement.h"
#include "error.h"

void function_arg_drop(FunctionArg* me) {
    initialized_binding_drop(&my initializer);
}

void function_impl_drop(FunctionImpl* me) {
    generic_drop(&my generic);
    vec_function_arg_drop(&my args);
    type_drop(&my ret);
    lazy_type_drop(&my lazy_type);
    arc_declaration_drop(&my declaration);
    declaration_drop(&my this_declaration);
    block_drop(&my body);
}

void function_impl_add_names(FunctionImpl* me, Scope* scope, bool exported) {
    scope_init_declaration(scope, &my declaration, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported,
        .tag = my has_body ? DECLARATION_FUNCTION_IMPL : DECLARATION_FUNCTION_SIGNATURE,
        .generic = &my generic,
        .value_type = type_lazy(&my lazy_type),
    });
}

void function_impl_lookup_names(FunctionImpl* me, bool this_is_any, ScopeStack* names, ErrorCtx* errs) {
    my lazy_type.tag = LAZY_TYPE_FUNCTION;

    // Handle generics (includes adding names)
    Scope generic_scope = scope_init(errs);
    scope_push_generic_to_stack(&generic_scope, &my generic, names);

    Scope function_scope = scope_init_function(errs);

    if (this_is_any) {
        declaration_init(&my this_declaration, (DeclProps){
            .tag = DECLARATION_VARIABLE,
            .value_type = type_any,
        });
        function_scope.this_declaration = &my this_declaration;
    }

    // Loop through all arguments and add them to the scope
    c_foreach(i, vec_function_arg, my args) {
        initialized_binding_add_names(&i.ref->initializer, &function_scope, (BindingAddNamesProps){
            .abstract_literals = true,
            .is_function_arg = true,
            .full = i.ref->span,
        });
        if (i.ref->initializer.binding.tag == BINDING_IDENTIFIER &&
            span_equals(i.ref->initializer.binding.content_span, "this")
        ) {
            function_scope.this_declaration = i.ref->initializer.binding.declaration.get;
        }
    }

    scope_push_to_stack(&function_scope, names);
    // Now that names for all arguments are added, resolve all names
    c_foreach(i, vec_function_arg, my args) {
        initialized_binding_lookup_names(&i.ref->initializer, names, errs);
    }
    scope_pop_from_stack(&function_scope);

    // Processes names in the function body and return type. don't push a stack frame since the toplevel function body
    // should be the same namespace as the arguments
    block_add_names(&my body, &function_scope);

    scope_push_to_stack(&function_scope, names);
    block_lookup_names(&my body, names, errs);
    type_lookup_names(&my ret, names, errs);
    scope_pop_from_stack(&function_scope);

    scope_drop(&function_scope);
    scope_drop(&generic_scope);
}

static Type function_impl_make_callable_sans_inferred_ret(FunctionImpl const* me) {
    VecCallableArg args = vec_callable_arg_init();
    c_foreach(i, vec_function_arg, my args) {
        vec_callable_arg_push(&args, (CallableArg){
            .optional = i.ref->explicit_optional || i.ref->initializer.value.tag != EXPR_IMPLICIT_ANY,
            .type = type_refcopy(binding_get_effective_type(&i.ref->initializer.binding)),
            .source = i.ref, // Safe because FunctionImpls are always owned by the AST, and more args will not be added
        });
    }
    return type_callable(c_new(CallableType, {
        .generic = generic_clone(my generic),
        .args = args,
        .ret = type_ref_always(&my ret),
        .is_constructor = my is_constructor,
        .is_abstract_constructor = my is_abstract_constructor
    }));
}

Type function_impl_into_type(FunctionImpl me) {
    // PERF: Maybe allocate the CallableType and FunctionImpl in the same memory block?
    FunctionImpl* impl = c_new(FunctionImpl, me);
    Type type = function_impl_make_callable_sans_inferred_ret(impl);
    type.callable->owned_function_impl = impl;
    return type;
}

Type function_impl_evaluate_type(FunctionImpl* me) {
    Type result = function_impl_make_callable_sans_inferred_ret(me);

    if (!my ret_explicit && my has_body) {
        Type ret_type;
        if (my evaluating_ret) {
            ret_type = type_self_referential_error;
        } else {
            my evaluating_ret = true;
            ret_type = block_evaluate_return_type(&my body);
            my evaluating_ret = false;
        }

        type_drop(&result.callable->ret);
        if (ret_type.tag == TYPE_NEVER) {
            // This will need to be improved
            result.callable->ret = type_void;
        } else {
            result.callable->ret = type_move(&ret_type);
            type_abstract_literals(&result.callable->ret);
        }

        type_drop(&ret_type);
    }

    return result;
}

void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs) {
    c_foreach(i, vec_function_arg, my args) {
        if (!i.ref->initializer.binding.has_explicit_type &&
            i.ref->initializer.value.tag == EXPR_IMPLICIT_ANY
        ) {
            if (get_error_config()->no_implicit_any) {
                binding_add_implicit_any_type_errors(&i.ref->initializer.binding, true, errs);
            }
        }
        initialized_binding_typecheck(&i.ref->initializer, errs);
    }

    type_typecheck(lazy_type_get(&my lazy_type), errs);

    if (my newline_before_arrow) {
        error_ctx_error(errs, my arrow_span, 1200, "Line terminator not permitted before arrow.");
    }

    BlockTypecheckCtx ctx = {
        .specified_return = my ret_explicit ? &my ret : &type_any,
    };
    block_typecheck(&my body, ctx, errs);
}
