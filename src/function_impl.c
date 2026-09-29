#include "function_impl.h"
#include "source_file.h"
#include "callable_type.h"
#include "generic.h"
#include "statement.h"
#include "error.h"
#include "several_types.h"
#include "ty/ty.h"
#include "program.h"
#include "unused.h"
#include "class_decl.h"

void function_arg_drop(FunctionArg* me) {
    vec_expr_drop(&my decorators);
    initialized_binding_drop(&my initializer);
}

void function_impl_drop(FunctionImpl* me) {
    generic_drop(&my generic);
    vec_function_arg_drop(&my args);
    type_drop(&my ret);
    if (my predicate) {
        type_drop(&my predicate->type);
        free(my predicate);
    }
    lazy_type_drop(&my lazy_type);
    block_drop(&my body);
    free(my yields);
    if (my jsdoc_signature_type) {
        type_drop(my jsdoc_signature_type);
        free(my jsdoc_signature_type);
    }
    for (u32 i = 0; i < my jsdoc_overload_count; i++) type_drop(&my jsdoc_overloads[i]);
    free(my jsdoc_overloads);
    free(my jsdoc_param_issues);
}

void function_impl_add_names(FunctionImpl* me, Scope* scope, DeclExported exported, bool hoisted, bool declared) {
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported == DECL_EXPORTED_YES,
        .default_export = exported == DECL_EXPORTED_YES_DEFAULT,
        .kind = SYMBOL_DECL_FUNCTION,
        .node = me,
        .function_scoped = hoisted,
        .is_ambient = declared,
    });
    if (scope->file && scope->file->is_js) scope_mark_jsdoc_class(scope->binder, my symbol, me);
}

// tsc's requiresScopeChange (useOuterVariableScopeInParameter): whether the downlevel emit of a parameter has to move it
// into the function body, where the body's variables are in scope: temporaries of `?.` and `??` below ES2020, of an
// object rest element below ES2017, the static fields of a class expression (unless class fields are standard).
// Doesn't look into functions, nor into the values of object literal members and non-static class members
static bool expr_requires_scope_change(Expr const* me);

static bool exprs_require_scope_change(VecExpr const* exprs) {
    for_each (i, vec_expr, exprs) {
        if (expr_requires_scope_change(i)) return true;
    }
    return false;
}

static bool binding_requires_scope_change(Binding const* me, bool object_element) {
    if (object_element && my rest && compiler_options()->target < SCRIPT_TARGET_ES2017) return true;
    if (my default_value && expr_requires_scope_change(my default_value)) return true;
    switch (my tag) {
        case BINDING_NOOP:
        case BINDING_IDENTIFIER:
            return false;
        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) {
                if (binding_requires_scope_change(i, false)) return true;
            }
            return false;
        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                if (i->computed_key && expr_requires_scope_change(i->computed_key)) return true;
                if (binding_requires_scope_change(&i->target, true)) return true;
            }
            return false;
    }
    return false;
}

static bool class_requires_scope_change(ClassDecl const* me) {
    CompilerOptions const* options = compiler_options();
    bool standard_fields = options->use_define_for_class_fields && options->target >= SCRIPT_TARGET_ES2022;
    if (exprs_require_scope_change(&my decorators) || expr_requires_scope_change(&my extends)) return true;
    for_each (i, vec_class_item, &my items) {
        if (exprs_require_scope_change(&i->decorators)) return true;
        switch (i->tag) {
            case CLASS_ITEM_FIELD:
                if (i->field.is_static && !standard_fields) return true;
                if (i->field.computed_key && expr_requires_scope_change(i->field.computed_key)) return true;
                break;
            case CLASS_ITEM_METHOD:
                if (i->method.computed_key && expr_requires_scope_change(i->method.computed_key)) return true;
                break;
            default:
                break;
        }
    }
    return false;
}

static bool expr_requires_scope_change(Expr const* me) {
    bool below_es2020 = compiler_options()->target < SCRIPT_TARGET_ES2020;
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
        case EXPR_LITERAL:
        case EXPR_IDENTIFIER:
        case EXPR_REGEX:
        case EXPR_PRIVATE_NAME:
        case EXPR_FUNCTION:
        // TODO: JSX isn't looked into
        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT:
        case EXPR_JSX_ATTRIBUTES:
            return false;
        case EXPR_BINARY_OP:
            if (my binary_op->op == BINARY_OP_EH_EH && below_es2020) return true;
            return expr_requires_scope_change(&my binary_op->left) || expr_requires_scope_change(&my binary_op->right);
        case EXPR_UNARY_OP:
            return expr_requires_scope_change(&my unary_op->expr);
        case EXPR_PROPERTY:
            if (my property->optional_chain && below_es2020) return true;
            return expr_requires_scope_change(&my property->expr) ||
                (my property->form == PROPERTY_EXPR_BRACKET && expr_requires_scope_change(&my property->prop));
        case EXPR_GENERIC_PARAMS:
            return expr_requires_scope_change(&my generic_params->expr);
        case EXPR_AS_TYPE:
            return expr_requires_scope_change(&my as_type->expr);
        case EXPR_TERNARY:
            return expr_requires_scope_change(&my ternary->condition) || expr_requires_scope_change(&my ternary->yes) ||
                expr_requires_scope_change(&my ternary->no);
        case EXPR_CALL:
            if (my call->optional_chain && below_es2020) return true;
            return expr_requires_scope_change(&my call->callable) || exprs_require_scope_change(&my call->args);
        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &my object) {
                if (i->computed_key && expr_requires_scope_change(i->computed_key)) return true;
                if (i->tag == OBJECT_ITEM_SPREAD && expr_requires_scope_change(&i->value)) return true;
                if (i->tag == OBJECT_ITEM_SHORTHAND && i->shorthand_default && expr_requires_scope_change(i->shorthand_default)) {
                    return true;
                }
            }
            return false;
        case EXPR_ARRAY:
            return exprs_require_scope_change(&my array);
        case EXPR_CLASS_DECL:
            return class_requires_scope_change(my class_decl);
        case EXPR_SPREAD:
            return expr_requires_scope_change(my spread);
        case EXPR_TEMPLATE:
            return (my template_->tag && expr_requires_scope_change(my template_->tag)) ||
                exprs_require_scope_change(&my template_->substitutions);
    }
    return false;
}

// From ES2015 on the parameters don't see the body's variables, unless the emit moves them into the body
static bool parameters_see_body(FunctionImpl const* me) {
    if (compiler_options()->target < SCRIPT_TARGET_ES2015) return false; // (They see them anyway)
    for_each (i, vec_function_arg, &my args) {
        if (binding_requires_scope_change(&i->initializer.binding, false)) return true;
        if (expr_requires_scope_change(&i->initializer.value)) return true;
    }
    return false;
}

void function_impl_lookup_names_as(FunctionImpl* me, FunctionFlowKind kind, bool this_is_any, Scope* names, ErrorCtx* errs) {
    lazy_type_bind(&my lazy_type, LAZY_TYPE_FUNCTION);
    my ambient = names->ambient;
    if (my is_async) {
        my promise_decl = scope_get_type(names, c_sv("Promise"), nullptr);
    }
    for_each (i, vec_function_arg, &my args) {
        for_each (decorator, vec_expr, &i->decorators) {
            expr_lookup_names(decorator, names, errs);
        }
    }

    // A control flow container. tsc: bindContainer()
    my is_expression = kind == FUNCTION_IS_EXPRESSION;
    my no_outer_type_params = !names->generics && !my is_expression && !my is_method && !span_valid(my member_name) &&
        !(my full_span.source && my full_span.source->is_js);
    FlowBuilder* outer = names->flow;
    bool is_iife = outer->next_function_is_iife && my has_body;
    outer->next_function_is_iife = false;
    FlowBuilder flow;
    flow_builder_init(&flow, outer, names, me, errs);
    flow.constructor_of = is_iife ? outer->constructor_of : my constructed_class;
    if (is_iife) {
        // Part of the flow around it, a `return` is a jump to the end of the body
        flow.start = outer->start;
        flow.current = outer->current;
        flow.dead = outer->dead;
        flow.has_flow_effects = outer->has_flow_effects;
        flow.initializing = outer->initializing;
        flow.parameter_initializing = outer->parameter_initializing;
        flow.parameter_function_scope = outer->parameter_function_scope;
    } else {
        if (kind == FUNCTION_IS_EXPRESSION) my outer_flow = flow_builder_reference(outer, span_null);
        // Signatures and function types (the libs are full of them) have nothing that flows
        if (my has_body) flow.current = flow_new_start(&flow, me, kind == FUNCTION_IS_EXPRESSION);
    }
    if (is_iife || (my is_constructor && my has_body)) flow.return_target = flow_new_branch_label(&flow);

    unused_register(names, UNUSED_FUNCTION, me, span_null);
    // Handle generics (includes adding names)
    if (!generic_empty(&my generic)) {
        Span owner = span_valid(my name) ? my name :
            span_valid(my member_name) ? my member_name : span_valid(my variable_name) ? my variable_name :
            sv_make_span(c_sv("(Anonymous function)"));
        // (A class method is named through its class, an object literal's isn't)
        ClassDecl const* method_of = span_valid(my member_name) && !my is_expression ? scope_enclosing_class(names) : nullptr;
        names = scope_new_generic(names, &my generic, owner, method_of ? class_decl_display_name(method_of) : span_null, errs);
    }

    Scope* function_scope = scope_new(names, SCOPE_FUNCTION);
    function_scope->flow = &flow;
    function_scope->function = me;
    function_scope->strict |= my has_body && block_has_use_strict(&my body);

    if (this_is_any) function_scope->this_ref = (ThisRef){ .kind = THIS_ANY, .function = me, .type_boundary = true };

    // Loop through all arguments and add them to the scope
    for_each (i, vec_function_arg, &my args) {
        i->initializer.binding.optional_parameter = i->explicit_optional;
        initialized_binding_add_names(&i->initializer, function_scope, (BindingAddNamesProps){
            .abstract_literals = true,
            .is_function_arg = true,
            .full = i->span,
        });
        if (i->initializer.binding.tag == BINDING_IDENTIFIER &&
            span_equals(i->initializer.binding.content_span, "this")
        ) {
            function_scope->this_ref = (ThisRef){
                .kind = THIS_PARAMETER, .symbol = i->initializer.binding.symbol, .type_boundary = this_is_any,
            };
        }
    }

    // The toplevel of the function body is the same scope as the arguments
    block_add_names(&my body, function_scope);

    function_scope->no_super = my is_constructor ? NO_SUPER_CONSTRUCTOR_ARGS : NO_SUPER_NONE;
    Scope* parameter_scope = scope_new(function_scope, SCOPE_BLOCK);
    parameter_scope->parameters = true;
    parameter_scope->parameters_see_body = my has_body && parameters_see_body(me);
    TypeFlow const* saved_flow = type_flow_enter_parameter();
    for_each (i, vec_function_arg, &my args) {
        initialized_binding_lookup_names_parameter(&i->initializer, parameter_scope, errs);
    }
    type_flow_restore(saved_flow);
    function_scope->no_super = NO_SUPER_NONE;
    // (Not a JSDoc `@returns`, which sees the body's types: tsc's resolveNameHelper exempts a JSDoc lastLocation)
    if (my has_body && !(names->file && names->file->is_js)) {
        Scope* return_scope = scope_new(function_scope, SCOPE_BLOCK);
        return_scope->return_type = true;
        type_lookup_names(&my ret, return_scope, errs);
    } else {
        type_lookup_names(&my ret, function_scope, errs);
    }
    for (u32 i = 0; i < my jsdoc_overload_count; i++) {
        for_each (item, vec_generic_item, &my jsdoc_overloads[i].callable->function->generic.items) {
            if (!item->jsdoc_shared) continue;
            for_each (own, vec_generic_item, &my generic.items) {
                if (csview_equals_sv(own->name.sv, item->name.sv)) item->symbol_ref = own->owned_symbol;
            }
        }
        type_lookup_names(&my jsdoc_overloads[i], function_scope, errs);
    }
    if (my jsdoc_signature_type) {
        type_lookup_names(my jsdoc_signature_type, names, errs);
        for_each (i, vec_function_arg, &my args) i->initializer.jsdoc_typed_by = me;
    }
    if (my predicate) {
        TypePredicateNode* predicate = my predicate;
        if (predicate->has_type) type_lookup_names(&predicate->type, function_scope, errs);
        for_each (i, vec_function_arg, &my args) {
            if (predicate->is_this || !csview_equals_sv(i->initializer.binding.content_span.sv, predicate->name.sv)) continue;
            predicate->param_index = (i32)i_index;
            if (i->initializer.binding.tag == BINDING_IDENTIFIER) predicate->symbol = i->initializer.binding.symbol;
        }
    }
    saved_flow = type_flow_enter_statements();
    block_lookup_names_as(&my body, true, function_scope, errs);
    type_flow_restore(saved_flow);

    my end_flow = nullptr;
    if (!flow_is_unreachable(flow.current) && my has_body) my end_flow = flow.current;
    my has_explicit_return = flow.has_explicit_return;
    my has_bare_return = flow.has_bare_return;
    my yields = flow.yields;
    my yield_count = flow.yield_count;
    flow.yields = nullptr;
    if (flow.return_target) {
        flow_add_antecedent(&flow, flow.return_target, flow.current);
        flow.current = flow_finish_label(flow.return_target);
        if (my is_constructor) my return_flow = flow.current;
    }
    flow_builder_finish(&flow, c_sv("function"), span_valid(my name) ? my name : my full_span);
    if (is_iife && !outer->dead) {
        outer->current = flow.current;
        outer->has_flow_effects |= flow.has_flow_effects;
    }
}

TypeNode function_impl_into_type(FunctionImpl me) {
    // PERF: Maybe allocate the CallableType and FunctionImpl in the same memory block?
    me.is_type = true;
    return type_callable(c_new(CallableType, { .function = c_new(FunctionImpl, me) }));
}

Ty const* function_impl_enter_return_context(FunctionImpl const* me) {
    Checker* checker = current_checker;
    Ty const* outer = checker->return_context;
    if (my is_generator) {
        // What the annotation (or the contextual type) says the generator returns
        TyIterationTypes const* context = ty_generator_context(checker, me);
        checker->return_context = context && !(context->return_type->flags & (TYF_ANY | TYF_VOID)) ? context->return_type : nullptr;
        if (my is_async && checker->return_context) {
            // Awaited, or a PromiseLike of that (tsc's getContextualTypeForReturnExpression for an async generator)
            Ty const* context_type = checker->return_context;
            TyList members = context_type->kind == TY_UNION ? context_type->composite.types : (TyList){ .data = &context_type, .len = 1 };
            TyBuf parts = {0};
            for (u32 i = 0; i < members.len; i++) ty_buf_push(&parts, ty_awaited_no_alias(checker, members.data[i]));
            Ty const* awaited = ty_union(checker, parts.data, parts.len, nullptr, (TyList){0});
            ty_buf_drop(&parts);
            Ty const* both[2] = { awaited, ty_promise_like_of(checker, awaited) };
            checker->return_context = both[1] ? ty_union(checker, both, 2, nullptr, (TyList){0}) : awaited;
        }
    } else if (my is_async) {
        // The awaited annotation, or a PromiseLike of it (tsc's getContextualTypeForReturnExpression)
        Ty const* annotated = my ret_explicit ? ty_unwrapped_return_type(checker, me) : nullptr;
        checker->return_context = nullptr;
        if (annotated && !(annotated->flags & (TYF_ANY | TYF_VOID))) {
            Ty const* both[2] = { annotated, ty_promise_like_of(checker, annotated) };
            checker->return_context = ty_union(checker, both, 2, nullptr, (TyList){0});
        }
        if (!my ret_explicit) {
            // The contextual return type's awaited type, or a PromiseLike of it (tsc's getContextualReturnType for
            // async functions)
            Ty const* contextual = ty_return_expressions_context(checker, me);
            Ty const* awaited = contextual ? ty_async_contextual_awaited(checker, contextual) : nullptr;
            if (awaited && !(awaited->flags & (TYF_ANY | TYF_VOID))) {
                Ty const* both[2] = { awaited, ty_promise_like_of(checker, awaited) };
                checker->return_context = both[1] ? ty_union(checker, both, 2, nullptr, (TyList){0}) : awaited;
            }
        }
    } else if (my ret_explicit || my setter_type) {
        Ty const* annotated = ty_from_node(checker, my ret_explicit ? &my ret : my setter_type, (LowerCtx){ .generic = true });
        checker->return_context = annotated->flags & (TYF_ANY | TYF_VOID) ? nullptr : annotated;
    } else {
        checker->return_context = ty_return_expressions_context(checker, me);
    }
    return outer;
}

Ty const* function_impl_evaluate_type(FunctionImpl* me) {
    return ty_function_type(current_checker, me);
}

void function_impl_pair_setter(FunctionImpl* setter, FunctionImpl* getter) {
    for_each (i, vec_function_arg, &setter->args) {
        bool is_this = i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this");
        if (is_this) continue;
        if (!i->initializer.binding.has_explicit_type) i->initializer.getter = getter;
        else if (!getter->ret_explicit) getter->setter_type = &i->initializer.binding.explicit_type;
        return;
    }
}

// tsc's checkGrammarParameterList(): the first misplaced rest parameter, `?`, or required parameter after an optional one
static bool function_impl_check_grammar_parameters(FunctionImpl const* me, ErrorCtx* errs) {
    bool seen_optional = false;
    for_each (i, vec_function_arg, &my args) {
        bool has_initializer = i->initializer.value.tag != EXPR_IMPLICIT_ANY;
        Span name = i->initializer.binding.content_span;
        if (i->rest) {
            if (i_index != vec_function_arg_size(&my args) - 1) {
                error_ctx_grammar_error_d(errs, i->rest_span, 1014, "A rest parameter must be last in a parameter list.");
                return true;
            }
            if (span_valid(i->question_span)) {
                error_ctx_grammar_error_d(errs, i->question_span, 1047, "A rest parameter cannot be optional.");
                return true;
            }
            if (has_initializer) {
                error_ctx_grammar_error_d(errs, name, 1048, "A rest parameter cannot have an initializer.");
                return true;
            }
        } else if (i->explicit_optional) {
            seen_optional = true;
            if (span_valid(i->question_span) && has_initializer) {
                error_ctx_grammar_error_d(errs, name, 1015, "Parameter cannot have question mark and initializer.");
                return true;
            }
        } else if (seen_optional && !has_initializer) {
            error_ctx_grammar_error_d(errs, name, 1016, "A required parameter cannot follow an optional parameter.");
            return true;
        }
    }
    return false;
}

static bool is_this_parameter(FunctionArg const* arg) {
    return arg->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(arg->initializer.binding.content_span, "this");
}

// tsc's checkGrammarAccessor(), the parameter part
static bool function_impl_check_grammar_accessor(FunctionImpl const* me, ErrorCtx* errs) {
    bool is_getter = my accessor == 1;
    isize count = vec_function_arg_size(&my args);
    isize expected = is_getter ? 0 : 1;
    bool has_this = count == expected + 1 && is_this_parameter(vec_function_arg_at(&my args, 0));
    if (count != expected && !has_this) {
        if (is_getter) {
            error_ctx_grammar_error_d(errs, my member_name, 1054, "A 'get' accessor cannot have parameters.");
        } else {
            error_ctx_grammar_error_d(errs, my member_name, 1049, "A 'set' accessor must have exactly one parameter.");
        }
        return true;
    }
    if (is_getter) return false;
    if (my ret_explicit && !my ret.from_jsdoc) { // (a JSDoc `@returns {void}` is fine)
        error_ctx_grammar_error_d(errs, my member_name, 1095, "A 'set' accessor cannot have a return type annotation.");
        return true;
    }
    FunctionArg const* value = vec_function_arg_at(&my args, has_this ? 1 : 0);
    if (value->rest) {
        error_ctx_grammar_error_d(errs, value->rest_span, 1053, "A 'set' accessor cannot have rest parameter.");
        return true;
    }
    if (span_valid(value->question_span)) {
        error_ctx_grammar_error_d(errs, value->question_span, 1051, "A 'set' accessor cannot have an optional parameter.");
        return true;
    }
    if (value->initializer.value.tag != EXPR_IMPLICIT_ANY) {
        error_ctx_grammar_error_d(errs, value->initializer.binding.content_span, 1052,
            "A 'set' accessor parameter cannot have an initializer.");
        return true;
    }
    return false;
}

// `type F = ({ a: string }) => void` renames `a` to `string` (tsc's checkPotentialUncheckedRenamedBindingElementsInTypes):
// a renaming nothing uses in a signature without a body is an error, even without noUnusedLocals
static void binding_check_unused_renamings(Binding const* me, FunctionArg const* arg, ErrorCtx* errs) {
    if (my tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &my array) binding_check_unused_renamings(i, arg, errs);
    } else if (my tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &my object) {
            Binding const* target = &i->target;
            if (target->tag != BINDING_IDENTIFIER || target->rest || span_eq(i->source_span, target->content_span)) {
                binding_check_unused_renamings(target, arg, errs);
                continue;
            }
            if (target->symbol && target->symbol->referenced) continue;
            Error* e = error_ctx_error_d(errs, target->content_span, 2842, "'", target->content_span,
                "' is an unused renaming of '", i->source_span, "'. Did you intend to use it as a type annotation?");
            if (e && !arg->initializer.binding.has_explicit_type) {
                Span end = { .source = arg->span.source, .sv = csview_from_n(arg->span.sv.buf + arg->span.sv.size, 0) };
                error_add_related_d(e, end, 2843, "We can only write a type for '", i->source_span,
                    "' by adding a type for the entire parameter here.");
            }
        }
    }
}

void function_impl_typecheck_signature(FunctionImpl const* me, bool is_type, ErrorCtx* errs) {
    if (!my has_body && my full_span.source && !my full_span.source->is_declaration) {
        for_each (i, vec_function_arg, &my args) binding_check_unused_renamings(&i->initializer.binding, i, errs);
    }
    generic_typecheck(&my generic, errs);
    bool grammar_error = function_impl_check_grammar_parameters(me, errs);
    if (!grammar_error && my accessor && span_valid(my member_name)) {
        grammar_error = function_impl_check_grammar_accessor(me, errs);
    }
    if (!grammar_error && span_valid(my asterisk)) {
        // tsc's checkGrammarForGenerator()
        if (my ambient) {
            error_ctx_grammar_error_d(errs, my asterisk, 1221, "Generators are not allowed in an ambient context.");
        } else if (!my has_body) {
            error_ctx_grammar_error_d(errs, my asterisk, 1222, "An overload signature cannot be declared as a generator.");
        }
    }

    for_each (i, vec_function_arg, &my args) {
        bool has_initializer = i->initializer.value.tag != EXPR_IMPLICIT_ANY;
        bool is_pattern = i->initializer.binding.tag == BINDING_ARRAY || i->initializer.binding.tag == BINDING_OBJECT;
        if (i->is_parameter_property && is_pattern) {
            // (tsc's checkGrammarModifiers)
            error_ctx_grammar_error_d(errs, i->span, 1187, "A parameter property may not be declared using a binding pattern.");
        } else if (i->is_parameter_property && i->rest) {
            error_ctx_grammar_error_d(errs, i->span, 1317, "A parameter property cannot be declared using a rest parameter.");
        }
        if (i->is_parameter_property && !(my is_constructor && my has_body)) {
            error_ctx_error_d(errs, i->span, 2369, "A parameter property is only allowed in a constructor implementation.");
        }
        if (!has_initializer && i->explicit_optional && is_pattern && my has_body) {
            error_ctx_error_d(errs, i->span, 2463, "A binding pattern parameter cannot be optional in an implementation signature.");
        }
        if ((is_type || !my has_body) && has_initializer) {
            // (tsc's checkVariableLikeDeclaration: overloads and ambient functions too)
            error_ctx_error_d(
                errs,
                i->span,
                2371,
                "A parameter initializer is only allowed in a function or constructor implementation."
            );
        }

        if (i->rest) {
            // `(...args: infer A) => any` is fine: the implied constraint of A is `unknown[]`
            Binding const* binding = &i->initializer.binding;
            Checker* checker = current_checker;
            Ty const* rest_type = ty_binding_type(checker, binding, (LowerCtx){ .generic = true });
            // (A pattern's type is what the pattern implies, tsc's checkParameter doesn't check it)
            if (binding->tag == BINDING_IDENTIFIER && !ty_extends_array(checker, rest_type)) {
                error_ctx_error_d(errs, i->span, 2370, "A rest parameter must be of an array type.");
            }
        }
    }

    // (tsc's checkTypePredicate; TODO: TS1230 for a name bound by a parameter's binding pattern)
    TypePredicateNode const* predicate = my predicate;
    if (predicate && predicate->has_type && !predicate->jsdoc) type_typecheck(&my predicate->type, errs);
    if (predicate && !predicate->is_this && !predicate->jsdoc && span_valid(predicate->name)) {
        if (predicate->param_index >= 0) {
            FunctionArg const* arg = vec_function_arg_at(&my args, (isize_t)predicate->param_index);
            if (arg->rest) {
                error_ctx_error_d(errs, predicate->name, 1229, "A type predicate cannot reference a rest parameter.");
            } else if (predicate->has_type && span_valid(predicate->type_span)) {
                Checker* checker = current_checker;
                ty_check_type_predicate(checker, ty_from_node(checker, &predicate->type, (LowerCtx){ .generic = true }),
                    ty_binding_type(checker, &arg->initializer.binding, (LowerCtx){ .generic = true }), predicate->type_span,
                    errs);
            }
        } else {
            bool any_pattern = false;
            for_each (i, vec_function_arg, &my args) any_pattern |= i->initializer.binding.tag != BINDING_IDENTIFIER;
            if (!any_pattern) {
                error_ctx_error_d(errs, predicate->name, 1225, "Cannot find parameter '", predicate->name, "'.");
            }
        }
    }
}

static bool ty_maybe_void(Ty const* type) {
    if (type->flags & TYF_VOID) return true;
    if (type->kind != TY_UNION) return false;
    for (u32 i = 0; i < type->composite.types.len; i++) {
        if (type->composite.types.data[i]->flags & TYF_VOID) return true;
    }
    return false;
}

bool function_impl_return_void_any_or_undefined(FunctionImpl const* me, Ty const* return_type) {
    Ty const* unwrapped = ty_unwrap_return_type(current_checker, me, return_type);
    return unwrapped && (ty_maybe_void(unwrapped) || unwrapped->flags & (TYF_ANY | TYF_UNDEFINED));
}

// tsc: checkAllCodePathsInNonVoidFunctionReturnOrThrow(). Only when it is certain that control can fall off the end
// (ty_function_end_reachability())
static void function_impl_check_end(FunctionImpl* me, ErrorCtx* errs) {
    if (!my has_body || my is_constructor || my accessor == 2) return;
    Checker* checker = current_checker;
    if (ty_function_end_reachability(checker, me) != FLOW_REACH_YES) return;

    // What tsc reports a function-like node at: its name, the head of an arrow function, else its first token
    Span name = span_valid(my name) ? my name : my member_name;
    Span at = my ret_explicit ? my ret_span : function_error_span(me);

    if (my accessor == 1 && !my has_explicit_return) {
        if (span_valid(name)) error_ctx_error_d(errs, name, 2378, "A 'get' accessor must return a value.");
        return;
    }
    Ty const* type = nullptr;
    if (my ret_explicit) {
        // (tsc's unwrapReturnType: an async function's is what it awaits to, error if that's nothing)
        type = ty_unwrapped_return_type(checker, me);
        if (!type && my is_async && !my is_generator) type = ty_intrinsic(checker, TY_ANY);
        if (type && (ty_maybe_void(type) || type->flags & (TYF_ANY | TYF_UNDEFINED))) return;
    }
    if (type && type->flags & TYF_NEVER) {
        error_ctx_error_d(errs, at, 2534, "A function returning 'never' cannot have a reachable end point.");
    } else if (type && !my has_explicit_return) {
        error_ctx_error_d(errs, at, 2355,
            "A function whose declared type is neither 'undefined', 'void', nor 'any' must return a value.");
    } else if (type && compiler_options()->strict_null_checks &&
        !ty_is_related(checker, ty_intrinsic(checker, TY_UNDEFINED), type, TY_RELATION_ASSIGNABLE)
    ) {
        error_ctx_error_d(errs, at, 2366,
            "Function lacks ending return statement and return type does not include 'undefined'.");
    } else if (compiler_options()->no_implicit_returns) {
        if (!type) {
            if (!my has_explicit_return) return;
            if (function_impl_return_void_any_or_undefined(me, ty_function_return_type(checker, me))) return;
        }
        error_ctx_error_d(errs, at, 7030, "Not all code paths return a value.");
    }
}

Span function_error_span(FunctionImpl const* me) {
    if (span_valid(my name)) return my name;
    if (span_valid(my member_name)) return my member_name;
    if (span_valid(my arrow_span)) {
        Span ret = my full_span;
        if (!my expression_body) {
            const char* start = span_start_ptr(ret);
            const char* line_end = memchr(start, '\n', ret.sv.size);
            if (line_end) ret.sv.size = line_end - start;
            while (ret.sv.size && (ret.sv.buf[ret.sv.size - 1] == '\r')) ret.sv.size--;
        }
        return ret;
    }
    return block_first_token(my full_span);
}

// The helpers async functions and generators are emitted with (tsc's checkSignatureDeclaration)
static void function_check_emit_helpers(FunctionImpl const* me, ErrorCtx* errs) {
    if (!my has_body || my ambient || (!my is_async && !my is_generator)) return;
    ScriptTarget target = compiler_options()->target;
    Span at = function_error_span(me);
    if (my is_async && my is_generator && target < SCRIPT_TARGET_ES2018) {
        ty_check_emit_helpers(current_checker, at, EMIT_HELPER_AWAIT | EMIT_HELPER_ASYNC_GENERATOR, errs);
    }
    if (my is_async && !my is_generator && target < SCRIPT_TARGET_ES2017) {
        ty_check_emit_helpers(current_checker, at, EMIT_HELPER_AWAITER, errs);
    }
    if (target < SCRIPT_TARGET_ES2015) ty_check_emit_helpers(current_checker, at, EMIT_HELPER_GENERATOR, errs);
}

// tsc's checkCollisionWithArgumentsInGeneratedCode(): below ES2015 a rest parameter is made from `arguments`
static void function_check_arguments_collision(FunctionImpl const* me, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    if (options->target >= SCRIPT_TARGET_ES2015 || options->no_emit || my ambient || !my has_body) return;
    if (vec_function_arg_is_empty(&my args) || !vec_function_arg_back(&my args)->rest) return;
    for_each (i, vec_function_arg, &my args) {
        Binding const* binding = &i->initializer.binding;
        if (binding->tag == BINDING_IDENTIFIER && csview_equals(binding->content_span.sv, "arguments")) {
            error_ctx_error_d(errs, i->span, 2396,
                "Duplicate identifier 'arguments'. Compiler uses 'arguments' to initialize rest parameters.");
        }
    }
}

void function_impl_check_implicit_any_return(FunctionImpl const* me, Span name, Span at, ErrorCtx* errs) {
    if (my private_ambient || my has_body || my ret_explicit || my accessor || my is_constructor || !span_valid(name)) return;
    if (!compiler_options()->no_implicit_any || (name.source && name.source->is_js)) return;
    error_ctx_error_d(errs, at, 7010, "'", name, "', which lacks return-type annotation, implicitly has an 'any' return type.");
}

// tsc's getReturnTypeOfSignature when the inferred return type references itself: TS7023 at the name (a function
// expression's is what it's assigned to, getNameOfDeclaration), TS7024 without one
static void function_check_circular_return(FunctionImpl const* me, ErrorCtx* errs) {
    if (!compiler_options()->no_implicit_any || !my has_body || my ret_explicit || my jsdoc_signature_type) return;
    if (!ty_function_return_circular(current_checker, me)) return;
    Span name = span_valid(my name) ? my name : span_valid(my member_name) ? my member_name : my variable_name;
    if (span_valid(name)) {
        error_ctx_error_d(errs, name, 7023, "'", name, "' implicitly has return type 'any' because it does not have a return "
            "type annotation and is referenced directly or indirectly in one of its return expressions.");
    } else {
        error_ctx_error_d(errs, function_error_span(me), 7024, "Function implicitly has return type 'any' because it does not "
            "have a return type annotation and is referenced directly or indirectly in one of its return expressions.");
    }
}

// Parameters without a type (and default) that nothing gives a type are implicitly any
void function_impl_check_implicit_any_parameters(FunctionImpl const* me, ErrorCtx* errs) {
    if (!compiler_options()->no_implicit_any || my private_ambient || my jsdoc_type) return;
    for_each (i, vec_function_arg, &my args) {
        if (i->initializer.binding.has_explicit_type || i->initializer.value.tag != EXPR_IMPLICIT_ANY) continue;
        if (my jsdoc_signature_type) ty_function_apply_jsdoc_type(current_checker, me, nullptr);
        if (i->initializer.getter || ty_contextual_parameter_type(current_checker, &i->initializer)) continue;
        if (i->name_is_type && my is_type && !my is_constructor) {
            error_ctx_error_d(errs, i->span, 7051, "Parameter has a name but no type. Did you mean 'arg", (u32)i_index, ": ",
                i->initializer.binding.content_span, i->rest ? "[]" : "", "'?");
            continue;
        }
        if (i->rest && i->initializer.binding.tag == BINDING_IDENTIFIER) {
            error_ctx_error_d(errs, i->span, 7019, "Rest parameter '", i->initializer.binding.content_span,
                "' implicitly has an 'any[]' type.");
        } else {
            binding_add_implicit_any_type_errors(&i->initializer.binding, true, errs);
        }
    }
}

// tsc's reportErrorsFromWidening for an inferred return type: null and undefined in it widen to any without
// strictNullChecks (`return null`, `return [null]`, `return { a: null }`). TS7018 at the object literal properties
// that do, else TS7010 (TS7011 for an unnamed function expression) with the widened type. A contextually typed
// function only if the contextual return type is generic (shouldReportErrorsFromWideningWithContextualSignature)
static void function_check_widened_return(FunctionImpl const* me, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    if (!options->no_implicit_any || options->strict_null_checks || my ret_explicit || !my has_body) return;
    if (my is_generator || my is_constructor || my accessor || my jsdoc_typed) return;
    Checker* checker = current_checker;
    Ty const* unwidened = ty_function_return_before_widening(checker, me);
    if (!unwidened) return;
    if (my is_expression && !ty_function_widening_reportable(checker, me)) return;
    if (binding_report_widening_errors_in_type(unwidened, errs)) return;
    cstr type = ty_to_cstr(checker, ty_widened_nullable(checker, ty_widened_objects(checker, unwidened)));
    Span name = span_valid(my name) ? my name : my member_name;
    if (span_valid(name)) {
        error_ctx_error_d(errs, name, 7010, "'", name, "', which lacks return-type annotation, implicitly has an '", type,
            "' return type.");
    } else {
        error_ctx_error_d(errs, function_error_span(me), 7011,
            "Function expression, which lacks return-type annotation, implicitly has an '", type, "' return type.");
    }
    cstr_drop(&type);
}

void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs) {
    function_check_emit_helpers(me, errs);
    function_impl_check_implicit_any_parameters(me, errs);
    // (The parameter decorators of a class member are the class's to check: they are calls with synthetic arguments,
    // which is what gives them their contextual type)
    bool class_member = !my is_expression && (span_valid(my member_name) || my constructed_class);
    for_each (i, vec_function_arg, &my args) {
        if (!class_member) {
            for_each (decorator, vec_expr, &i->decorators) {
                expr_typecheck(decorator, errs);
            }
        }
        // (The type of a JSDoc `@this {T}` isn't checked: tsc's checkJSDocThisTag only looks at its host)
        if (i->jsdoc_this) continue;
        initialized_binding_typecheck_parameter(&i->initializer, i->span, errs);
        if (i->initializer.binding.tag == BINDING_ARRAY && i->initializer.value.tag != EXPR_IMPLICIT_ANY &&
            !i->initializer.binding.has_explicit_type
        ) {
            binding_add_padded_implicit_any_errors(&i->initializer.binding,
                ty_expr_type(current_checker, &i->initializer.value), errs);
        }
    }

    function_impl_typecheck_signature(me, false, errs);
    if (my ret_explicit) type_typecheck(&my ret, errs);
    ty_check_async_return_annotation(current_checker, me, errs);
    function_check_arguments_collision(me, errs);
    // (tsc's checkUnmatchedJSDocParameters; a body that uses `arguments` may take more, TS8029 isn't reported)
    for (u32 i = 0; i < my jsdoc_param_issue_count && !my uses_arguments; i++) {
        JsDocParamIssue const* issue = &my jsdoc_param_issues[i];
        if (span_valid(issue->left)) {
            error_ctx_error_d(errs, issue->name, 8032, "Qualified name '", issue->name,
                "' is not allowed without a leading '@param {object} ", issue->left, "'.");
        } else {
            error_ctx_error_d(errs, issue->name, 8024, "JSDoc '@param' tag has name '", issue->name,
                "', but there is no parameter with that name.");
        }
    }

    if (my newline_before_arrow) {
        error_ctx_error_d(errs, my arrow_span, 1200, "Line terminator not permitted before arrow.");
    }

    // tsc's checkGrammarStatementInAmbientContext for the body, which then covers the statements directly in it
    if (my ambient && my has_body && !my expression_body && my full_span.source && !my full_span.source->has_parse_errors) {
        // (The body's `{`: the first after the parameters and the return type)
        char const* at = span_valid(my ret_span) && span_end_ptr(my ret_span) > span_end_ptr(my args_span) ?
            span_end_ptr(my ret_span) : span_end_ptr(my args_span);
        char const* end = span_end_ptr(my full_span);
        while (at && at < end && *at != '{') at++;
        Span body = at && at < end ? block_first_token(span_make(my full_span.source, at, end)) : span_null;
        if (span_valid(body)) error_ctx_error_d(errs, body, 1183, "An implementation cannot be declared in ambient contexts.");
    }
    function_check_widened_return(me, errs);
    BlockTypecheckCtx ctx = {
        .ambient = my ambient,
        .ambient_reported = true,
        .in_function = true,
        // TODO: returns in async functions and generators aren't checked, the declared type wraps what's returned
        .specified_return = my ret_explicit && !my is_async && !my is_generator ? &my ret :
            my setter_type ? my setter_type : &type_any,
        .constructed_class = my is_constructor ? my constructed_class : nullptr,
        .unwrapped_return = my ret_explicit && (my is_async || my is_generator) ? ty_unwrapped_return_type(current_checker, me) : nullptr,
        .await_returned = my is_async,
        .function = me,
        .in_setter = my accessor == 2,
    };
    if (my ret_explicit && my is_generator) ty_check_generator_annotation(current_checker, me, errs);
    Ty const* outer_context = function_impl_enter_return_context(me);
    block_typecheck(&my body, ctx, errs);
    current_checker->return_context = outer_context;
    function_impl_check_end(me, errs);
    function_check_circular_return(me, errs);
}
