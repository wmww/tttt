#include "function_impl.h"
#include "scope_stack.h"
#include "callable_type.h"
#include "generic.h"
#include "statement.h"
#include "error.h"
#include "several_types.h"
#include "ty/ty.h"

void function_arg_drop(FunctionArg* me) {
    vec_expr_drop(&my decorators);
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
    lazy_type_bind(&my lazy_type, LAZY_TYPE_FUNCTION);
    if (my is_async) {
        my promise_decl = scope_stack_get_type(names, c_sv("Promise"));
    }
    for_each (i, vec_function_arg, &my args) {
        for_each (decorator, vec_expr, &i->decorators) {
            expr_lookup_names(decorator, names, errs);
        }
    }

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
    for_each (i, vec_function_arg, &my args) {
        initialized_binding_add_names(&i->initializer, &function_scope, (BindingAddNamesProps){
            .abstract_literals = true,
            .is_function_arg = true,
            .full = i->span,
        });
        if (i->initializer.binding.tag == BINDING_IDENTIFIER &&
            span_equals(i->initializer.binding.content_span, "this")
        ) {
            function_scope.this_declaration = i->initializer.binding.declaration.get;
        }
    }

    scope_push_to_stack(&function_scope, names);
    // Now that names for all arguments are added, resolve all names
    for_each (i, vec_function_arg, &my args) {
        initialized_binding_lookup_names(&i->initializer, names, errs);
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
    for_each (i, vec_function_arg, &my args) {
        vec_callable_arg_push(&args, (CallableArg){
            .optional = i->explicit_optional || i->initializer.value.tag != EXPR_IMPLICIT_ANY,
            .type = type_refcopy(binding_get_effective_type(&i->initializer.binding)),
            .source = i, // Safe because FunctionImpls are always owned by the AST, and more args will not be added
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

Ty const* function_impl_enter_return_context(FunctionImpl const* me) {
    Checker* checker = current_checker;
    Ty const* outer = checker->return_context;
    if (my is_async || my is_generator) {
        checker->return_context = nullptr; // TODO: the awaited/yielded parts of the annotation
    } else if (my ret_explicit) {
        Ty const* annotated = ty_from_node(checker, &my ret, (LowerCtx){ .generic = true });
        checker->return_context = annotated->flags & (TYF_ANY | TYF_VOID) ? nullptr : annotated;
    } else {
        checker->return_context = ty_contextual_return_type(checker, me);
    }
    return outer;
}

Type function_impl_evaluate_type(FunctionImpl* me) {
    return type_new(ty_function_type(current_checker, me));
}

void function_impl_typecheck_signature(FunctionImpl const* me, bool is_type, ErrorCtx* errs) {
    generic_typecheck(&my generic, errs);

    bool found_optional = false;
    bool required_after_optional = false;
    bool rest_not_last = false;
    for_each (i, vec_function_arg, &my args) {
        bool has_initializer = i->initializer.value.tag != EXPR_IMPLICIT_ANY;
        if (is_type && has_initializer) {
            error_ctx_error_d(
                errs,
                i->initializer.value.outer_span,
                2371,
                "A parameter initializer is only allowed in a function or constructor implementation."
            );
        }

        if (i->rest && i_index != vec_function_arg_size(&my args) - 1 && !rest_not_last) {
            rest_not_last = true;
            error_ctx_error_d(errs, i->rest_span, 1014, "A rest parameter must be last in a parameter list.");
        }

        if (i->rest) {
        } else if (i->explicit_optional) {
            found_optional = true;
        } else if (found_optional && !has_initializer && !required_after_optional) {
            required_after_optional = true;
            error_ctx_error_d(
                errs,
                i->initializer.binding.content_span,
                1016,
                "A required parameter cannot follow an optional parameter."
            );
        }

        if (i->rest) {
            // `(...args: infer A) => any`: an `infer` type in this position is implicitly constrained to an array
            Type const* written = binding_get_effective_type(&i->initializer.binding);
            Checker* checker = current_checker;
            if (written->tag != TYPE_INFERRED &&
                !ty_extends_array(checker, ty_from_legacy(checker, written, (LowerCtx){ .generic = true }))
            ) {
                error_ctx_error_d(errs, i->span, 2370, "A rest parameter must be of an array type.");
            }
            if (has_initializer) {
                error_ctx_error_d(
                    errs,
                    i->initializer.binding.content_span,
                    1048,
                    "A rest parameter cannot have an initializer."
                );
            }
        }
    }
}

void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs) {
    for_each (i, vec_function_arg, &my args) {
        if (!i->initializer.binding.has_explicit_type &&
            i->initializer.value.tag == EXPR_IMPLICIT_ANY
        ) {
            if (compiler_options()->no_implicit_any && !ty_contextual_parameter_type(current_checker, &i->initializer)) {
                binding_add_implicit_any_type_errors(&i->initializer.binding, true, errs);
            }
        }
        for_each (decorator, vec_expr, &i->decorators) {
            expr_typecheck(decorator, errs);
        }
        initialized_binding_typecheck(&i->initializer, errs);
    }

    function_impl_typecheck_signature(me, false, errs);

    if (my newline_before_arrow) {
        error_ctx_error_d(errs, my arrow_span, 1200, "Line terminator not permitted before arrow.");
    }

    BlockTypecheckCtx ctx = {
        // TODO: returns in async functions and generators aren't checked, the declared type wraps what's returned
        .specified_return = my ret_explicit && !my is_async && !my is_generator ? &my ret : &type_any,
    };
    Ty const* outer_context = function_impl_enter_return_context(me);
    block_typecheck(&my body, ctx, errs);
    current_checker->return_context = outer_context;
}
