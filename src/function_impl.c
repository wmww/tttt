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
}

void function_impl_add_names(FunctionImpl* me, Scope* scope, DeclExported exported, bool hoisted) {
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported == DECL_EXPORTED_YES,
        .default_export = exported == DECL_EXPORTED_YES_DEFAULT,
        .kind = SYMBOL_DECL_FUNCTION,
        .node = me,
        .function_scoped = hoisted,
    });
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
    names = scope_new_generic(names, &my generic, errs);

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
    TypeFlow const* saved_flow = type_flow_enter_parameter();
    for_each (i, vec_function_arg, &my args) {
        initialized_binding_lookup_names_parameter(&i->initializer, parameter_scope, errs);
    }
    type_flow_restore(saved_flow);
    function_scope->no_super = NO_SUPER_NONE;
    type_lookup_names(&my ret, function_scope, errs);
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
    } else if (my is_async) {
        // The awaited annotation (tsc's getContextualTypeForReturnExpression also has a PromiseLike of it)
        Ty const* annotated = my ret_explicit ? ty_unwrapped_return_type(checker, me) : nullptr;
        checker->return_context = annotated && !(annotated->flags & (TYF_ANY | TYF_VOID)) ? annotated : nullptr;
        if (!my ret_explicit) {
            // The contextual return type's awaited type, or a PromiseLike of it (tsc's getContextualReturnType for
            // async functions)
            Ty const* contextual = ty_contextual_return_type(checker, me);
            Ty const* awaited = contextual ? ty_async_contextual_awaited(checker, contextual) : nullptr;
            if (awaited && !(awaited->flags & (TYF_ANY | TYF_VOID))) {
                Ty const* both[2] = { awaited, ty_promise_like_of(checker, awaited) };
                checker->return_context = both[1] ? ty_union(checker, both, 2, nullptr, (TyList){0}) : awaited;
            }
        }
    } else if (my ret_explicit) {
        Ty const* annotated = ty_from_node(checker, &my ret, (LowerCtx){ .generic = true });
        checker->return_context = annotated->flags & (TYF_ANY | TYF_VOID) ? nullptr : annotated;
    } else {
        checker->return_context = ty_contextual_return_type(checker, me);
    }
    return outer;
}

Ty const* function_impl_evaluate_type(FunctionImpl* me) {
    return ty_function_type(current_checker, me);
}

void function_impl_pair_setter(FunctionImpl* setter, FunctionImpl const* getter) {
    for_each (i, vec_function_arg, &setter->args) {
        bool is_this = i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this");
        if (is_this) continue;
        if (!i->initializer.binding.has_explicit_type) i->initializer.getter = getter;
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
    if (my ret_explicit) {
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

void function_impl_typecheck_signature(FunctionImpl const* me, bool is_type, ErrorCtx* errs) {
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
}

static bool ty_maybe_void(Ty const* type) {
    if (type->flags & TYF_VOID) return true;
    if (type->kind != TY_UNION) return false;
    for (u32 i = 0; i < type->composite.types.len; i++) {
        if (type->composite.types.data[i]->flags & TYF_VOID) return true;
    }
    return false;
}

// tsc: checkAllCodePathsInNonVoidFunctionReturnOrThrow(). Only when it is certain that control can fall off the end
// (ty_function_end_reachability())
static void function_impl_check_end(FunctionImpl* me, ErrorCtx* errs) {
    if (!my has_body || my is_constructor || my accessor == 2) return;
    Checker* checker = current_checker;
    if (ty_function_end_reachability(checker, me) != FLOW_REACH_YES) return;

    // What tsc reports a function-like node at: its name, the head of an arrow function, else its first token
    Span name = span_valid(my name) ? my name : my member_name;
    Span at = my ret_explicit ? my ret_span : name;
    if (!span_valid(at) && span_valid(my arrow_span)) at = span_between(my args_span, my arrow_span);
    if (!span_valid(at)) at = my args_span; // TODO: the `function` keyword

    if (my accessor == 1 && !my has_explicit_return) {
        if (span_valid(name)) error_ctx_error_d(errs, name, 2378, "A 'get' accessor must return a value.");
        return;
    }
    Ty const* type = nullptr;
    if (my ret_explicit) {
        if (my is_async || my is_generator) return; // TODO: what the annotation says about the returned value
        type = ty_from_node(checker, &my ret, (LowerCtx){ .generic = true });
        if (ty_maybe_void(type) || type->flags & (TYF_ANY | TYF_UNDEFINED)) return;
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
            if (!my has_explicit_return || my is_async || my is_generator) return;
            Ty const* inferred = ty_function_return_type(checker, me);
            if (ty_maybe_void(inferred) || inferred->flags & (TYF_ANY | TYF_UNDEFINED)) return;
        }
        error_ctx_error_d(errs, at, 7030, "Not all code paths return a value.");
    }
}

// tsc's getErrorSpanForNode() of a function: its name, else an arrow function up to the end of its first line, else
// its first token
static Span function_error_span(FunctionImpl const* me) {
    if (span_valid(my name)) return my name;
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

void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs) {
    function_check_emit_helpers(me, errs);
    for_each (i, vec_function_arg, &my args) {
        if (!i->initializer.binding.has_explicit_type &&
            i->initializer.value.tag == EXPR_IMPLICIT_ANY
        ) {
            if (compiler_options()->no_implicit_any && !i->initializer.getter &&
                !ty_contextual_parameter_type(current_checker, &i->initializer)
            ) {
                if (i->rest && i->initializer.binding.tag == BINDING_IDENTIFIER) {
                    error_ctx_error_d(errs, i->span, 7019, "Rest parameter '", i->initializer.binding.content_span,
                        "' implicitly has an 'any[]' type.");
                } else {
                    binding_add_implicit_any_type_errors(&i->initializer.binding, true, errs);
                }
            }
        }
        for_each (decorator, vec_expr, &i->decorators) {
            expr_typecheck(decorator, errs);
        }
        initialized_binding_typecheck(&i->initializer, errs);
        if (i->initializer.binding.tag == BINDING_ARRAY && i->initializer.value.tag != EXPR_IMPLICIT_ANY &&
            !i->initializer.binding.has_explicit_type
        ) {
            binding_add_padded_implicit_any_errors(&i->initializer.binding,
                ty_expr_type(current_checker, &i->initializer.value), errs);
        }
    }

    function_impl_typecheck_signature(me, false, errs);
    if (my ret_explicit) type_typecheck(&my ret, errs);

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
    BlockTypecheckCtx ctx = {
        .ambient = my ambient,
        .ambient_reported = true,
        .in_function = true,
        // TODO: returns in async functions and generators aren't checked, the declared type wraps what's returned
        .specified_return = my ret_explicit && !my is_async && !my is_generator ? &my ret : &type_any,
        .constructed_class = my is_constructor ? my constructed_class : nullptr,
        .unwrapped_return = my ret_explicit && (my is_async || my is_generator) ? ty_unwrapped_return_type(current_checker, me) : nullptr,
        .await_returned = my is_async,
    };
    if (my ret_explicit && my is_generator) ty_check_generator_annotation(current_checker, me, errs);
    Ty const* outer_context = function_impl_enter_return_context(me);
    block_typecheck(&my body, ctx, errs);
    current_checker->return_context = outer_context;
    function_impl_check_end(me, errs);
}
