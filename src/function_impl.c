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

const VecExpr function_arg_no_decorators = {0};

void function_arg_drop(FunctionArg* me) {
    if (my decorators) {
        vec_expr_drop(my decorators);
        ast_delete(my decorators);
    }
    ast_delete(my jsdoc_span);
    ast_delete(my jsdoc_question);
    initialized_binding_drop(&my initializer);
}

void function_impl_drop(FunctionImpl* me) {
    generic_drop(&my generic);
    vec_function_arg_drop(&my args);
    type_drop(&my ret);
    if (my predicate) {
        type_drop(&my predicate->type);
        ast_delete(my predicate);
    }
    if (my deferred) {
        // (A deferred body's block is in its DeferredBody, in the arena with it: empty unless materialized)
        if (my body_.deferred) block_drop(&my body_.deferred->block);
    } else {
        block_drop(&my body_);
    }
    FunctionImplRare* rare = my rare;
    if (!rare) return;
    ast_free(rare->yields_, sizeof(Expr const*) * rare->yield_count_);
    if (rare->jsdoc_signature_type) {
        type_drop(rare->jsdoc_signature_type);
        ast_delete(rare->jsdoc_signature_type);
    }
    for (u32 i = 0; i < rare->jsdoc_overload_count; i++) type_drop(&rare->jsdoc_overloads[i]);
    ast_delete(rare->jsdoc_overloads);
    ast_delete(rare->jsdoc_param_issues);
    for (u32 i = 0; i < rare->jsdoc_unmatched_count; i++) type_drop(&rare->jsdoc_unmatched[i].type);
    ast_delete(rare->jsdoc_unmatched);
    ast_delete(rare);
}

const FunctionImplRare function_impl_no_rare = {0};

Span function_impl_span_get(FunctionImpl const* me, FunctionSpan span) {
    if (!span.start) return span_null;
    return span_from_sv(my span_source, csview_from_n(source_file_start(my span_source) + span.start - 1, span.length));
}

void function_impl_span_set(FunctionImpl* me, FunctionSpan* field, Span span) {
    if (!span_valid(span)) {
        *field = (FunctionSpan){0};
        return;
    }
    if (!my span_source) my span_source = span_source(span);
    panic_if(span_source(span) != my span_source, "a function's spans in different files");
    *field = (FunctionSpan){ (u32)(span.buf - source_file_start(span_source(span))) + 1, (u32)span.size };
}

FunctionImplRare* function_impl_rare_mut(FunctionImpl* me) {
    if (!my rare) my rare = ast_new(FunctionImplRare, {0});
    return my rare;
}

void function_impl_add_names(FunctionImpl* me, Scope* scope, DeclStatementProps props, bool hoisted) {
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = props.exported == DECL_EXPORTED_YES,
        .default_export = props.exported == DECL_EXPORTED_YES_DEFAULT,
        .declare_keyword = span_valid(props.declare_keyword),
        .kind = SYMBOL_DECL_FUNCTION,
        .node = me,
        .function_scoped = hoisted,
        .is_ambient = props.declared,
    });
}

// tsc's requiresScopeChange (useOuterVariableScopeInParameter): whether the downlevel emit of a parameter has to move it
// into the function body, where the body's variables are in scope: temporaries of `?.` and `??` below ES2020, of an
// object rest element below ES2017, the static fields of a class expression (unless class fields are standard).
// Doesn't look into functions, nor into the values of object literal members and non-static class members
static bool pattern_declares(Binding const* pattern, Span name);
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
            return false;
        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT:
            if (expr_requires_scope_change(&my jsx->tag_name) || expr_requires_scope_change(&my jsx->attributes)) return true;
            for_each (i, vec_jsx_child, &my jsx->children) {
                if ((i->tag == JSX_CHILD_EXPR || i->tag == JSX_CHILD_ELEMENT) && expr_requires_scope_change(&i->expr)) return true;
            }
            return false;
        case EXPR_JSX_ATTRIBUTES:
            for_each (i, vec_jsx_attribute, &my jsx_attributes) {
                if ((i->tag == JSX_ATTRIBUTE_EXPR || i->tag == JSX_ATTRIBUTE_ELEMENT || i->tag == JSX_ATTRIBUTE_SPREAD) &&
                    expr_requires_scope_change(&i->value)
                ) {
                    return true;
                }
            }
            return false;
        case EXPR_BINARY_OP:
            if (my binary_op->op == BINARY_OP_EH_EH && below_es2020) return true;
            return expr_requires_scope_change(&my binary_op->left) || expr_requires_scope_change(&my binary_op->right);
        case EXPR_UNARY_OP:
            return expr_requires_scope_change(&my unary_op->expr);
        case EXPR_PROPERTY:
            if (my property->optional_chain && below_es2020) return true;
            return expr_requires_scope_change(&my property->expr) ||
                (my property->form == PROPERTY_EXPR_BRACKET && expr_requires_scope_change(&my property->index));
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
                if (i->tag == OBJECT_ITEM_SHORTHAND && object_item_rare(i)->shorthand_default && expr_requires_scope_change(object_item_rare(i)->shorthand_default)) {
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
    for_each (i, vec_function_arg, &my args) {
        if (binding_requires_scope_change(&i->initializer.binding, false)) return true;
        if (expr_requires_scope_change(initialized_binding_value(&i->initializer))) return true;
    }
    return false;
}

static bool is_this_parameter(FunctionArg const* arg) {
    return arg->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(arg->initializer.binding.content_span, "this");
}

bool function_arg_decoratable(FunctionImpl const* me, isize index, bool in_class_declaration) {
    return compiler_options()->experimental_decorators && in_class_declaration && my has_body && !my is_expression &&
        (my is_constructor || my is_method || my accessor == 2) &&
        !(index == 0 && is_this_parameter(vec_function_arg_at(&my args, 0)));
}

static DeferredBodies* deferred_bodies_of(SourceFile* file);
static void function_impl_lookup_body(FunctionImpl* me, FlowBuilder* flow, FlowBuilder* outer, Scope* function_scope,
    bool is_iife, bool js_constructor_candidate, ErrorCtx* errs);

void function_impl_lookup_names_as(FunctionImpl* me, FunctionFlowKind kind, bool this_is_any, Scope* names, ErrorCtx* errs) {
    // (a declaration or method too: what it returns is in the same context)
    if (names->flow && names->flow->contextual_depth) my syntactically_contextual = true;
    my ambient = names->ambient;
    if (my is_async) {
        function_impl_rare_mut(me)->promise_decl = scope_get_type(names, c_sv("Promise"), nullptr);
    }
    Scope* decorator_names = nullptr;
    for (isize a = 0; a < vec_function_arg_size(&my args); a++) {
        VecExpr const* decorators = function_arg_decorators(vec_function_arg_at(&my args, a));
        if (!vec_expr_size(decorators)) continue;
        // (What can't be decorated, tsc doesn't check: its names are still resolved for the baselines)
        ClassDecl const* class_decl = scope_enclosing_class(names);
        ErrorCtx* decorator_errs = function_arg_decoratable(me, a, class_decl && !class_decl->is_expression) ? errs :
            binder_discarded_errs(names->binder);
        for_each (decorator, vec_expr, decorators) {
            // (A class member's parameter decorators see the `this` outside the class, like the member's own)
            if (!decorator_names) {
                decorator_names = scope_new(names, SCOPE_BLOCK);
                decorator_names->member_decorator = true;
                scope_rare_mut(decorator_names)->decorated_function = me;
            }
            // (The function is the flow container of its parameters' decorators: what they read is an outer variable)
            bool member_name = names->flow && names->flow->member_name;
            if (names->flow) names->flow->member_name = true;
            expr_lookup_names(decorator, decorator_names, decorator_errs);
            if (names->flow) names->flow->member_name = member_name;
        }
    }

    // A control flow container. tsc: bindContainer()
    my is_expression = kind == FUNCTION_IS_EXPRESSION;
    my no_outer_type_params = !names->generics && !my is_expression && !my is_method && !span_valid(function_impl_member_name(me)) &&
        !(span_source(my full_span) && span_source(my full_span)->is_js);
    my no_enclosing_type_params = !names->generics && !(span_source(my full_span) && span_source(my full_span)->is_js) &&
        !scope_enclosing_class(names);
    FunctionImpl const* enclosing = names->function;
    my contextual_ancestor = !enclosing ? nullptr :
        enclosing->is_expression && ty_function_is_context_sensitive(enclosing) ? enclosing : enclosing->contextual_ancestor;
    FlowBuilder* outer = names->flow;
    bool is_iife = outer->next_function_is_iife && my has_body;
    outer->next_function_is_iife = false;
    FlowBuilder flow;
    flow_builder_init(&flow, outer, names, me, errs);
    flow.constructor_of = is_iife ? outer->constructor_of : function_impl_rare(me)->constructed_class;
    // (tsc checks a function expression's body later, outside any unreachable code around it)
    if (my is_expression) flow.within_unreachable = false;
    if (!is_iife) flow.container = me;
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
        bool continues = kind == FUNCTION_IS_EXPRESSION || my in_class_expression;
        if (continues) my outer_flow = flow_builder_reference(outer, span_null);
        // Signatures and function types (the libs are full of them) have nothing that flows
        if (my has_body) flow.current = flow_new_start(&flow, me, continues);
        // An async or generator IIFE (an inlined IIFE is the branch above) is in the container around it for the
        // checker (tsc's getControlFlowContainer skips IIFEs): flow walks always continue outside
        if (my has_body && my is_iife) flow.container_start = outer->container_start;
    }
    // (A JavaScript function may be a constructor function: `this.x = v` has its type at the end of it too)
    bool js_constructor_candidate = names->file && names->file->is_js && !function_impl_is_arrow(me) && !my is_method &&
        !my accessor && !span_valid(function_impl_member_name(me));
    if (is_iife || ((my is_constructor || js_constructor_candidate) && my has_body)) flow.return_target = flow_new_branch_label(&flow);

    unused_register(names, UNUSED_FUNCTION, me);
    // Handle generics (includes adding names)
    Scope* type_params_scope = nullptr;
    if (!generic_empty(&my generic)) {
        Span owner = span_valid(my name) ? my name :
            span_valid(function_impl_member_name(me)) ? function_impl_member_name(me) : span_valid(function_impl_variable_name(me)) ? function_impl_variable_name(me) :
            sv_make_span(c_sv("(Anonymous function)"));
        // (A class method is named through its class, an object literal's isn't)
        ClassDecl const* method_of = span_valid(function_impl_member_name(me)) && !my is_expression ? scope_enclosing_class(names) : nullptr;
        names = scope_new_generic(names, &my generic, owner, method_of ? class_decl_display_name(method_of) : span_null, &flow, errs);
        type_params_scope = names;
    }

    Scope* function_scope = scope_new(names, SCOPE_FUNCTION);
    function_scope->flow = &flow;
    if (type_params_scope) scope_rare_mut(type_params_scope)->type_params_function = function_scope;
    function_scope->function = me;
    // (A deferred body doesn't start with a string: parser.c's skip_body())
    function_scope->strict |= my has_body && !my deferred && block_has_use_strict(&my body_);

    if (this_is_any) scope_rare_mut(function_scope)->this_ref = (ThisRef){ .kind = THIS_ANY, .function = me, .type_boundary = true };

    // Loop through all arguments and add them to the scope
    for_each (i, vec_function_arg, &my args) {
        // (An unannotated `...x?` is any[] as it is: tsc's getWidenedTypeForVariableLikeDeclaration's anyArrayType; a function
        // expression's gets undefined anyway, its assignParameterType adds optionality after widening)
        i->initializer.binding.optional_parameter = (i->explicit_optional && !(i->rest && !i->initializer.binding.has_explicit_type && !my is_expression)) ||
            i->jsdoc_optional_initialized;
        initialized_binding_add_names(&i->initializer, function_scope, (BindingAddNamesProps){
            .abstract_literals = true,
            .is_function_arg = true,
            .full = i->span,
        });
        // (An arrow function's `this` stays the outer one, whatever parameter it declares: tsc's getThisContainer.
        // Only a first parameter is the `this` parameter: tsc's getThisParameter)
        if (i->initializer.binding.tag == BINDING_IDENTIFIER && !function_impl_is_arrow(me) && i == my args.data &&
            span_equals(i->initializer.binding.content_span, "this")
        ) {
            if (!i->initializer.binding.has_explicit_type && !i->jsdoc_this) initialized_binding_rare_mut(&i->initializer)->this_of = me;
            scope_rare_mut(function_scope)->this_ref = (ThisRef){
                .kind = THIS_PARAMETER, .symbol = i->initializer.binding.symbol, .type_boundary = this_is_any,
                // (A JavaScript `@this {T}`: `this.x = v` still declares x, see scope_add_this_property())
                .function = i->jsdoc_this ? me : nullptr,
            };
        }
    }

    // The toplevel of the function body is the same scope as the arguments
    if (!my deferred) block_add_names(&my body_, function_scope);
    // (A deferred body's names aren't there yet: what the signature's walk looks up through the function's scope is
    // recorded, so the materialization can tell whether one of them would have been found, scope_resolve())
    DeferredBody* deferred = my deferred ? my body_.deferred : nullptr;
    if (deferred) {
        if (my is_iife) log_error("A deferred body is an IIFE's");
        function_scope->deferred_body = true;
    }

    function_scope->no_super = my is_constructor ? NO_SUPER_CONSTRUCTOR_ARGS : NO_SUPER_NONE;
    ScopePendingMark pending_mark = scope_pending_mark(function_scope->binder);
    Scope* parameter_scope = scope_new(function_scope, SCOPE_BLOCK);
    parameter_scope->parameters = true;
    parameter_scope->parameters_see_body = my has_body && parameters_see_body(me);
    TypeFlow const* saved_flow = type_flow_enter_parameter();
    // (The function's own JSDoc types: tsc resolves their names from the function too, see identifier_type_lookup_names())
    bool js = names->file && names->file->is_js && !my is_type;
    Scope const* saved_host = js ? identifier_type_set_jsdoc_host(function_scope) : nullptr;
    for_each (i, vec_function_arg, &my args) {
        // (Without a body the initializer is only TS2371, but a pattern's elements under a written type look at its type
        // with strictNullChecks, which resolves its names: tsc's getBindingElementTypeFromParentType)
        Binding const* binding = &i->initializer.binding;
        bool looked_at = (binding->tag == BINDING_ARRAY || binding->tag == BINDING_OBJECT) && binding->has_explicit_type &&
            compiler_options()->strict_null_checks && !my ambient && !my is_type;
        initialized_binding_lookup_names_parameter(&i->initializer, parameter_scope, !my has_body && !looked_at, errs);
    }
    // (The `@param` tags that name no parameter: their types are where the parameters' are)
    for (u32 i = 0; i < function_impl_rare(me)->jsdoc_unmatched_count; i++) {
        type_lookup_names(&function_impl_rare(me)->jsdoc_unmatched[i].type, parameter_scope, errs);
    }
    if (js) identifier_type_set_jsdoc_host(saved_host);
    scope_release(parameter_scope, pending_mark);
    type_flow_restore(saved_flow);
    function_scope->no_super = NO_SUPER_NONE;
    if (my has_body) {
        ScopePendingMark return_mark = scope_pending_mark(function_scope->binder);
        Scope* return_scope = scope_new(function_scope, SCOPE_BLOCK);
        return_scope->return_type = true;
        type_lookup_names(&my ret, return_scope, errs);
        scope_release(return_scope, return_mark);
    } else {
        Scope const* saved_ret_host = js ? identifier_type_set_jsdoc_host(function_scope) : nullptr;
        type_lookup_names(&my ret, function_scope, errs);
        if (js) identifier_type_set_jsdoc_host(saved_ret_host);
    }
    for (u32 i = 0; i < function_impl_rare(me)->jsdoc_overload_count; i++) {
        type_lookup_names(&function_impl_rare(me)->jsdoc_overloads[i], function_scope, errs);
        // (A JSDoc `@overload`'s implementation is its host)
        if (function_impl_rare(me)->jsdoc_overloads[i].tag == TYPE_CALLABLE) {
            function_impl_rare_mut(function_impl_rare(me)->jsdoc_overloads[i].callable->function)->overload_implementation = me;
        }
    }
    if (function_impl_rare(me)->jsdoc_signature_type) {
        type_lookup_names(function_impl_rare(me)->jsdoc_signature_type, names, errs);
        for_each (i, vec_function_arg, &my args) initialized_binding_rare_mut(&i->initializer)->jsdoc_typed_by = me;
    }
    if (my predicate) {
        TypePredicateNode* predicate = my predicate;
        if (predicate->has_type) type_lookup_names(&predicate->type, function_scope, errs);
        for_each (i, vec_function_arg, &my args) {
            if (predicate->is_this || !name_equals(span_sv(i->initializer.binding.content_span), span_sv(predicate->name))) continue;
            predicate->param_index = (i32)i_index;
            if (i->initializer.binding.tag == BINDING_IDENTIFIER) predicate->symbol = i->initializer.binding.symbol;
        }
        // (No parameter of that name: tsc's getSymbolAtLocation resolves it like any name there, a binding element of
        // a parameter's pattern or an outer variable. Only the symbols baseline looks)
        if (!predicate->is_this && !predicate->symbol && predicate->param_index < 0 && span_valid(predicate->name)) {
            Symbol const* found = scope_resolve(function_scope, span_sv(predicate->name), SYMBOL_VALUE);
            if (found && !found->is_placeholder) predicate->symbol = found;
        }
    }
    if (deferred) {
        // The rest waits for the body (function_impl_lookup_deferred_body())
        function_scope->deferred_body = false;
        DeferredBodies* list = deferred_bodies_of(names->file);
        if (list->last) list->last->next = deferred;
        else list->first = deferred;
        list->last = deferred;
        list->count++;
        list->source_bytes += deferred->end - deferred->open;
        deferred->function = me;
        deferred->function_scope = function_scope;
        deferred->start = flow.start;
        deferred->container_start = flow.container_start;
        deferred->current = flow.current;
        deferred->return_target = flow.return_target;
        deferred->constructor_of = flow.constructor_of;
        deferred->property_context = flow.property_context;
        deferred->depth = flow.depth;
        deferred->contextual_depth = flow.contextual_depth;
        deferred->dead = flow.dead;
        deferred->has_flow_effects = flow.has_flow_effects;
        deferred->within_unreachable = flow.within_unreachable;
        deferred->ambient = flow.ambient;
        if (flow.yields || flow.yield_count || flow.true_target || flow.false_target || flow.active_labels ||
            flow.break_target || flow.continue_target || flow.exception_target || flow.initializing ||
            flow.parameter_initializing || flow.statement_list || flow.lookup_statements || flow.object_literal_depth ||
            flow.initializer_context || flow.in_type_query || flow.member_name || flow.method_name || flow.const_context
        ) {
            log_error("A deferred body's flow builder holds more than DeferredBody keeps");
        }
        return;
    }
    function_impl_lookup_body(me, &flow, outer, function_scope, is_iife, js_constructor_candidate, errs);
}

// The deferred function's file's list of them (made by its first)
static DeferredBodies* deferred_bodies_of(SourceFile* file) {
    if (!file->deferred_bodies) {
        DeferredBodies* list = c_new(DeferredBodies, { .errs = { .phase = ERROR_PHASE_RESOLVE_NAMES } });
        pthread_mutex_init(&list->lock, nullptr);
        file->deferred_bodies = list;
    }
    return file->deferred_bodies;
}

// The end of function_impl_lookup_names_as(): the body's walk and what it leaves on the function. `outer` is null for a
// deferred body
static void function_impl_lookup_body(FunctionImpl* me, FlowBuilder* flow, FlowBuilder* outer, Scope* function_scope,
    bool is_iife, bool js_constructor_candidate, ErrorCtx* errs
) {
    TypeFlow const* saved_flow = type_flow_enter_statements(function_scope->generics, !my expression_body,
        binder_arena(function_scope->binder));
    block_lookup_names_as(function_impl_body(me), function_scope, errs);
    type_flow_restore(saved_flow);

    my end_flow_ = nullptr;
    if (!flow_is_unreachable(flow->current) && my has_body) my end_flow_ = flow->current;
    my has_explicit_return_ = flow->has_explicit_return;
    my has_bare_return_ = flow->has_bare_return;
    if (flow->yields || my rare) {
        FunctionImplRare* rare = function_impl_rare_mut(me);
        // (Copied out exact into the AST's memory: a deferred body's go with its materialization)
        rare->yields_ = flow->yield_count ? ast_malloc(sizeof(Expr const*) * flow->yield_count) : nullptr;
        if (flow->yield_count) memcpy(rare->yields_, flow->yields, sizeof(Expr const*) * flow->yield_count);
        rare->yield_count_ = flow->yield_count;
    }
    free(flow->yields);
    flow->yields = nullptr;
    if (flow->return_target) {
        flow_add_antecedent(flow, flow->return_target, flow->current);
        flow->current = flow_finish_label(flow->return_target);
        if (my is_constructor || js_constructor_candidate) function_impl_rare_mut(me)->return_flow_ = flow->current;
    }
    flow_builder_finish(flow, c_sv("function"), span_valid(my name) ? my name : my full_span);
    if (is_iife && !outer->dead) {
        outer->current = flow->current;
        outer->has_flow_effects |= flow->has_flow_effects;
    }
}

// A rare struct a deferred body's walk made (what of it the walk can have made)
static void walk_rare_drop(void* rare) {
    ast_free(((FunctionImplRare*)rare)->yields_, sizeof(Expr const*) * ((FunctionImplRare*)rare)->yield_count_);
    ast_free(rare, sizeof(FunctionImplRare));
}

// Whether a signature's lookups of a name (`bits` and `meanings`, DeferredSignatureName's) would have seen the body's
// `found`. A body's value of the name of a type in the signature isn't found (`props: Props` and `...Props` in the
// body), nor a body's variable from the parameters (`inner: typeof fetch` and `const fetch`)
static bool deferred_signature_sees(uint8_t bits, u32 const* meanings, Symbol const* found) {
    if (!found || found->is_parameter) return false;
    for (u32 from = 0; from < 3; from++) {
        if (!(bits & (1u << from))) continue;
        u32 meaning = meanings[from] ? meanings[from] | SYMBOL_ALIAS : ~(u32)0;
        if (((found->flags & meaning) || found->is_placeholder) &&
            !function_local_hidden(found, meaning, from == DEFERRED_FROM_PARAMETERS, from == DEFERRED_FROM_RETURN_TYPE)
        ) {
            return true;
        }
    }
    return false;
}

bool function_impl_lookup_deferred_body(DeferredBody* body, ErrorCtx* errs) {
    FunctionImpl* me = body->function;
    Scope* function_scope = body->function_scope;
    // What flow_builder_init() and function_impl_lookup_names_as() made of the builder, up to the body
    FlowBuilder flow = {
        .arena = binder_arena(function_scope->binder),
        .errs = errs,
        .function = me,
        .container = me,
        .ambient = body->ambient,
        .depth = body->depth,
        .contextual_depth = body->contextual_depth,
        .container_start = body->container_start,
        .within_unreachable = body->within_unreachable,
        .constructor_of = body->constructor_of,
        .start = body->start,
        .current = body->current,
        .dead = body->dead,
        .has_flow_effects = body->has_flow_effects,
        .return_target = body->return_target,
        .property_context = body->property_context,
        .file = function_scope->file,
    };
    // (Flow node ids are for dumps only: a materialization's count on from where no file's do)
    flow.id_counter = 1u << 30;
    flow.next_id = &flow.id_counter;
    function_scope->flow = &flow;
    // What the walk sets on the function, as before the first walk (plan 78 stage 2: a materialization after a drop
    // finds the last one's; some are only set if unset). The rare struct the walk makes goes with the materialization
    me->end_flow_ = nullptr;
    me->has_explicit_return_ = false;
    me->has_bare_return_ = false;
    FunctionImplRare* rare = me->rare;
    if (rare) {
        ast_free(rare->yields_, sizeof(Expr const*) * rare->yield_count_);
        rare->yields_ = nullptr;
        rare->yield_count_ = 0;
        rare->return_flow_ = nullptr;
        rare->first_super_call_ = nullptr;
        rare->first_this_or_super_ = span_null;
    }
    u32 before = scope_locals(function_scope)->count;
    block_add_names(&body->block, function_scope);
    // Had the body's names been there, would the signature's walk have found one of them (or suggested one)?
    SymbolTable const* locals = scope_locals(function_scope);
    bool collides = false;
    csview* added = nullptr;
    for (u32 i = 0; i < body->signature_name_count && !collides; i++) {
        DeferredSignatureName const* name = &body->signature_names[i];
        collides = deferred_signature_sees(name->looked_up, name->meaning, symbol_table_get(locals, name->name));
        if (collides || !name->not_found || locals->count == before) continue;
        // (A body's name a suggestion could have picked: one the lookup would have seen within the spelling distance.
        // `Buffer` not found as a type doesn't suggest the body's `const buffer`)
        if (!added) added = c_new_n(csview, locals->count - before);
        for (u32 j = before; j < locals->count; j++) {
            bool seen = deferred_signature_sees(name->not_found, name->not_found_meaning, locals->entries[j].symbol);
            added[j - before] = seen ? (csview){ locals->entries[j].name_buf, locals->entries[j].name_len } : (csview){0};
        }
        collides = spelling_suggestion(name->name, added, locals->count - before) >= 0;
    }
    free(added);
    if (collides && debug_env.defer_stats) {
        Span at = span_valid(me->name) ? me->name : me->full_span;
        fprintf(stderr, "defer: collided %s:%u: %.*s\n", cstr_str(&span_source(at)->canonical_path), span_line_index(at) + 1,
            (int)(at.size > 60 ? 60 : at.size), at.buf);
    }
    function_impl_lookup_body(me, &flow, nullptr, function_scope, false, false, errs);
    function_scope->flow = nullptr;
    if (!rare && me->rare) deferred_bodies_undo_made((void**)&me->rare, nullptr, walk_rare_drop);
    return !collides;
}

TypeNode function_impl_into_type(FunctionImpl me) {
    me.is_type = true;
    // (One block, which type_drop() frees with the CallableType)
    struct { CallableType callable; FunctionImpl function; }* block = ast_new(typeof(*block), { .function = me });
    block->callable.function = &block->function;
    return type_callable(&block->callable);
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
        // (`void | PromiseLike<void>` for Promise<void>: `return new Promise(resolve => resolve())` infers void)
        // (`Promise<any>`'s is any: `this` in a returned object literal's methods is any, tsc's getThisTypeOfObjectLiteral)
        if (annotated && (annotated->flags & TYF_ANY)) {
            checker->return_context = annotated;
        } else if (annotated) {
            Ty const* both[2] = { annotated, ty_promise_like_of(checker, annotated) };
            checker->return_context = ty_union(checker, both, 2, nullptr, (TyList){0});
        }
        if (!my ret_explicit) {
            // The contextual return type's awaited type, or a PromiseLike of it (tsc's getContextualReturnType for
            // async functions)
            Ty const* context = ty_async_return_expressions_context(checker, ty_return_expressions_context(checker, me));
            if (context) checker->return_context = context;
        }
    } else if (my is_constructor && function_impl_rare(me)->constructed_class) {
        // A constructor's return expressions replace the instance: its type is their context (tsc's
        // getContextualReturnType through the constructor's signature)
        checker->return_context = ty_instance_type_of_class(checker, function_impl_rare(me)->constructed_class);
    } else if (my ret_explicit || function_impl_setter_type(me)) {
        Ty const* annotated = ty_from_node(checker, my ret_explicit ? &my ret : function_impl_setter_type(me), (LowerCtx){ .generic = true });
        // (An any annotation is a context too, which makes `this` in a returned object literal's methods any: tsc's
        // getContextualTypeForReturnExpression)
        checker->return_context = annotated->flags & TYF_VOID ? nullptr : annotated;
    } else {
        checker->return_context = ty_return_expressions_context(checker, me);
    }
    return outer;
}

// tsc's getAccessorThisParameter
static FunctionArg const* accessor_this_parameter(FunctionImpl const* accessor, bool setter) {
    if (vec_function_arg_size(&accessor->args) != (setter ? 2u : 1u)) return nullptr;
    FunctionArg const* first = vec_function_arg_at(&accessor->args, 0);
    return is_this_parameter(first) ? first : nullptr;
}

void function_impl_pair_setter(FunctionImpl* setter, FunctionImpl* getter, Expr const* key) {
    FunctionArg const* setter_this = accessor_this_parameter(setter, true);
    FunctionArg const* getter_this = accessor_this_parameter(getter, false);
    if (getter_this && !(vec_function_arg_size(&setter->args) && is_this_parameter(vec_function_arg_at(&setter->args, 0)) &&
        vec_function_arg_at(&setter->args, 0)->initializer.binding.has_explicit_type)) {
        function_impl_rare_mut(setter)->paired_this = getter_this;
    }
    if (setter_this && !(vec_function_arg_size(&getter->args) && is_this_parameter(vec_function_arg_at(&getter->args, 0)))) {
        function_impl_rare_mut(getter)->paired_this = setter_this;
    }
    for_each (i, vec_function_arg, &setter->args) {
        bool is_this = i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this");
        if (is_this) continue;
        if (!i->initializer.binding.has_explicit_type) {
            InitializedBindingRare* rare = initialized_binding_rare_mut(&i->initializer);
            rare->getter = getter;
            rare->getter_key = key;
        }
        else if (!getter->ret_explicit) {
            function_impl_rare_mut(getter)->setter_type = binding_type(&i->initializer.binding);
            function_impl_rare_mut(getter)->setter_key = key;
        }
        return;
    }
}

// The other accessor of a late-bound accessor's pair (see FunctionImplRare.late_accessor_class), null if none
static FunctionImpl const* late_accessor_pair(FunctionImpl const* me) {
    FunctionImplRare const* rare = function_impl_rare(me);
    if (!rare->late_accessor_item) return nullptr;
    ClassItem const* other = my accessor == 2 ? ty_class_accessor_getter(current_checker, rare->late_accessor_item) :
        ty_class_accessor_setter(current_checker, rare->late_accessor_class, rare->late_accessor_item);
    return other && other != rare->late_accessor_item ? &other->method.func : nullptr;
}

FunctionImpl const* function_impl_late_getter(FunctionImpl const* setter) {
    return late_accessor_pair(setter);
}

FunctionArg const* function_impl_paired_this(FunctionImpl const* me) {
    if (function_impl_rare(me)->paired_this) return function_impl_rare(me)->paired_this;
    FunctionImpl const* other = late_accessor_pair(me);
    if (!other) return nullptr;
    // (What function_impl_pair_setter() gives each)
    if (my accessor == 2) {
        FunctionArg const* getter_this = accessor_this_parameter(other, false);
        bool own = vec_function_arg_size(&my args) && is_this_parameter(vec_function_arg_at(&my args, 0)) &&
            vec_function_arg_at(&my args, 0)->initializer.binding.has_explicit_type;
        return getter_this && !own ? getter_this : nullptr;
    }
    FunctionArg const* setter_this = accessor_this_parameter(other, true);
    bool own = vec_function_arg_size(&my args) && is_this_parameter(vec_function_arg_at(&my args, 0));
    return setter_this && !own ? setter_this : nullptr;
}

TypeNode const* function_impl_setter_type(FunctionImpl const* me) {
    if (my accessor == 1 && !my ret_explicit && function_impl_rare(me)->late_accessor_item) {
        // (A late-bound getter's: its setter's first parameter but `this`, annotated)
        FunctionImpl const* setter = late_accessor_pair(me);
        if (!setter) return nullptr;
        for_each (i, vec_function_arg, &setter->args) {
            if (i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this")) continue;
            return i->initializer.binding.has_explicit_type ? binding_type(&i->initializer.binding) : nullptr;
        }
        return nullptr;
    }
    TypeNode const* type = function_impl_rare(me)->setter_type;
    Expr const* key = function_impl_rare(me)->setter_key;
    if (!type || !key || key->tag == EXPR_LITERAL) return type;
    Ty const* key_type = ty_expr_type(current_checker, (Expr*)key);
    return key_type->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL) ? type : nullptr;
}

// tsc's checkGrammarParameterList(): the first misplaced rest parameter, `?`, or required parameter after an optional one
static bool function_impl_check_grammar_parameters(FunctionImpl const* me, ErrorCtx* errs) {
    bool seen_optional = false;
    for_each (i, vec_function_arg, &my args) {
        bool has_initializer = initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY;
        Span name = i->initializer.binding.content_span;
        if (i->rest) {
            if (i_index != vec_function_arg_size(&my args) - 1) {
                error_ctx_grammar_error_d(errs, function_arg_rest_span(i), 1014, "A rest parameter must be last in a parameter list.");
                return true;
            }
            if (span_valid(function_arg_question_span(i))) {
                error_ctx_grammar_error_d(errs, function_arg_question_span(i), 1047, "A rest parameter cannot be optional.");
                return true;
            }
            if (has_initializer) {
                error_ctx_grammar_error_d(errs, name, 1048, "A rest parameter cannot have an initializer.");
                return true;
            }
        } else if (i->explicit_optional) {
            seen_optional = true;
            if (span_valid(function_arg_question_span(i)) && has_initializer) {
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


// tsc's checkGrammarAccessor(), the parameter part
static bool function_impl_check_grammar_accessor(FunctionImpl const* me, ErrorCtx* errs) {
    bool is_getter = my accessor == 1;
    if (my has_body && my is_abstract_member) {
        error_ctx_grammar_error_d(errs, function_impl_member_name(me), 1318, "An abstract accessor cannot have an implementation.");
        return true;
    }
    if (!generic_empty(&my generic)) {
        error_ctx_grammar_error_d(errs, function_impl_member_name(me), 1094, "An accessor cannot have type parameters.");
        return true;
    }
    isize count = vec_function_arg_size(&my args);
    isize expected = is_getter ? 0 : 1;
    bool has_this = count == expected + 1 && is_this_parameter(vec_function_arg_at(&my args, 0));
    if (count != expected && !has_this) {
        if (is_getter) {
            error_ctx_grammar_error_d(errs, function_impl_member_name(me), 1054, "A 'get' accessor cannot have parameters.");
        } else {
            error_ctx_grammar_error_d(errs, function_impl_member_name(me), 1049, "A 'set' accessor must have exactly one parameter.");
        }
        return true;
    }
    if (is_getter) return false;
    if (my ret_explicit) { // (A JSDoc `@returns` too: tsgo reparses it as the return type)
        error_ctx_grammar_error_d(errs, function_impl_member_name(me), 1095, "A 'set' accessor cannot have a return type annotation.");
        return true;
    }
    FunctionArg const* value = vec_function_arg_at(&my args, has_this ? 1 : 0);
    if (value->rest) {
        error_ctx_grammar_error_d(errs, function_arg_rest_span(value), 1053, "A 'set' accessor cannot have rest parameter.");
        return true;
    }
    if (span_valid(function_arg_question_span(value)) || value->jsdoc_question) {
        Span at = span_valid(function_arg_question_span(value)) ? function_arg_question_span(value) : *value->jsdoc_question;
        error_ctx_grammar_error_d(errs, at, 1051, "A 'set' accessor cannot have an optional parameter.");
        return true;
    }
    if (initialized_binding_value(&value->initializer)->tag != EXPR_IMPLICIT_ANY) {
        error_ctx_grammar_error_d(errs, function_impl_member_name(me), 1052,
            "A 'set' accessor parameter cannot have an initializer.");
        return true;
    }
    return false;
}

static bool type_node_queries(TypeNode const* node, Symbol const* symbol);

// Whether a `typeof x` in the signature's types names the symbol (tsc's isReferenced, which only a type query can set
// in a signature without a body; tttt tracks references only for noUnusedLocals)
static bool signature_queries(FunctionImpl const* function, Symbol const* symbol) {
    for_each (i, vec_function_arg, &function->args) {
        if (i->initializer.binding.has_explicit_type && type_node_queries(binding_type(&i->initializer.binding), symbol)) return true;
    }
    if (function->predicate && function->predicate->has_type && type_node_queries(&function->predicate->type, symbol)) return true;
    return type_node_queries(&function->ret, symbol);
}

static bool type_node_queries(TypeNode const* node, Symbol const* symbol) {
    switch (node->tag) {
        case TYPE_TYPEOF: {
            Expr const* e = node->typeof_->expr;
            while (true) {
                if (e->tag == EXPR_PROPERTY) e = &e->property->expr;
                else if (e->tag == EXPR_GENERIC_PARAMS) e = &e->generic_params->expr;
                else break;
            }
            return e->tag == EXPR_IDENTIFIER && e->identifier.symbol == symbol;
        }
        case TYPE_UNION: case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) if (type_node_queries(i, symbol)) return true;
            return false;
        case TYPE_ARRAY: return type_node_queries(node->array.element, symbol);
        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &node->tuple.elements) if (type_node_queries(&i->type, symbol)) return true;
            return false;
        case TYPE_IDENTIFIER:
            for_each (i, vec_spanned_type, &node->identifier->generic_params) if (type_node_queries(&i->type, symbol)) return true;
            return false;
        case TYPE_INDEXED: return type_node_queries(&node->indexed->interface, symbol) || type_node_queries(&node->indexed->index, symbol);
        case TYPE_KEYOF: return type_node_queries(&node->keyof->interface, symbol);
        case TYPE_TERNARY:
            return type_node_queries(&node->ternary->maybe_sub, symbol) || type_node_queries(&node->ternary->maybe_super, symbol) ||
                type_node_queries(&node->ternary->if_true, symbol) || type_node_queries(&node->ternary->if_false, symbol);
        case TYPE_CALLABLE: return signature_queries(node->callable->function, symbol);
        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &node->interface.members) if (type_node_queries(&i->type, symbol)) return true;
            return false;
        case TYPE_JSDOC: return type_node_queries(&node->jsdoc->type, symbol);
        default: return false;
    }
}

// `type F = ({ a: string }) => void` renames `a` to `string` (tsc's checkPotentialUncheckedRenamedBindingElementsInTypes):
// a renaming nothing uses in a signature without a body is an error, even without noUnusedLocals
static void binding_check_unused_renamings(FunctionImpl const* function, Binding const* me, FunctionArg const* arg, ErrorCtx* errs) {
    if (my tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &my array) binding_check_unused_renamings(function, i, arg, errs);
    } else if (my tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &my object) {
            Binding const* target = &i->target;
            if (target->tag != BINDING_IDENTIFIER || target->rest || span_eq(i->source_span, target->content_span)) {
                binding_check_unused_renamings(function, target, arg, errs);
                continue;
            }
            if (target->symbol && (target->symbol->referenced || signature_queries(function, target->symbol))) continue;
            // (declarationNameToString: a name recovery left missing is "(Missing)")
            csview name = target->content_span.size ? span_sv(target->content_span) : c_sv("(Missing)");
            Error* e = error_ctx_error_d(errs, target->content_span, 2842, "'", name,
                "' is an unused renaming of '", i->source_span, "'. Did you intend to use it as a type annotation?");
            if (e && !arg->initializer.binding.has_explicit_type) {
                Span end = span_in_sv(arg->span, csview_from_n(arg->span.buf + arg->span.size, 0));
                error_add_related_d(e, end, 2843, "We can only write a type for '", i->source_span,
                    "' by adding a type for the entire parameter here.");
            }
        }
    }
}

// tsc's checkGrammarForUseStrictSimpleParameterList (ES2016+): a "use strict" prologue in a body whose parameters
// aren't all simple (an initializer, a pattern, a rest): TS1346 on each of those, TS1347 on the directive
static void function_impl_check_use_strict_parameters(FunctionImpl const* me, ErrorCtx* errs) {
    if (compiler_options()->target < SCRIPT_TARGET_ES2016 || !my has_body || my expression_body) return;
    Statement const* directive = nullptr;
    for_each (i, vec_statement, &function_impl_body(me)->statements) {
        if (i->tag != STATEMENT_EXPR || i->expr.tag != EXPR_LITERAL || i->expr.literal.tag != VALUE_STRING ||
            expr_is_perenthisized(&i->expr) || *i->expr.inner_span.buf == '`'
        ) {
            break;
        }
        if (cstr_equals(&i->expr.literal.string, "use strict")) {
            directive = i;
            break;
        }
    }
    if (!directive) return;
    bool any = false;
    for_each (i, vec_function_arg, &my args) {
        bool simple = !i->rest && initialized_binding_value(&i->initializer)->tag == EXPR_IMPLICIT_ANY &&
            i->initializer.binding.tag == BINDING_IDENTIFIER;
        if (simple) continue;
        Error* e = error_ctx_error_d(errs, i->span, 1346, "This parameter is not allowed with 'use strict' directive.");
        error_add_related_d(e, directive->span, 1349, "'use strict' directive used here.");
        any = true;
    }
    if (!any) return;
    Error* e = error_ctx_error_d(errs, directive->span, 1347,
        "'use strict' directive cannot be used with non-simple parameter list.");
    bool first_one = true;
    for_each (i, vec_function_arg, &my args) {
        bool simple = !i->rest && initialized_binding_value(&i->initializer)->tag == EXPR_IMPLICIT_ANY &&
            i->initializer.binding.tag == BINDING_IDENTIFIER;
        if (simple) continue;
        if (first_one) error_add_related_d(e, i->span, 1348, "Non-simple parameter declared here.");
        else error_add_related_d(e, i->span, 6204, "and here.");
        first_one = false;
    }
}

// The defaults in a parameter's binding pattern of a signature without a body (tsc's checkVariableLikeDeclaration of
// a binding element: isParameterDeclaration and the containing function's body missing), at their names
static void report_pattern_initializers(Binding const* me, ErrorCtx* errs) {
    if (my tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &my array) {
            if (i->default_value) {
                error_ctx_error_d(errs, i->content_span, 2371,
                    "A parameter initializer is only allowed in a function or constructor implementation.");
            }
            report_pattern_initializers(i, errs);
        }
    } else if (my tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &my object) {
            if (i->target.default_value) {
                error_ctx_error_d(errs, i->target.content_span, 2371,
                    "A parameter initializer is only allowed in a function or constructor implementation.");
            }
            report_pattern_initializers(&i->target, errs);
        }
    }
}

// JSDoc `@overload`s: an overload without `@returns` implicitly returns any (TS7010, tsgo's reportImplicitAny of the
// reparsed declaration), and each has to fit the implementation (TS2394): both at the tag's name. `name` is the
// implementation's (a constructor's is its keyword, which the class knows)
void function_impl_check_jsdoc_overloads(FunctionImpl const* me, Span name, ErrorCtx* errs) {
    if (!function_impl_rare(me)->jsdoc_overload_count || !my has_body) return;
    FunctionImpl const* overloads[32];
    Span names[32];
    u32 count = 0;
    for (u32 i = 0; i < function_impl_rare(me)->jsdoc_overload_count && count < c_arraylen(overloads); i++) {
        if (function_impl_rare(me)->jsdoc_overloads[i].tag != TYPE_CALLABLE) continue;
        FunctionImpl const* overload = function_impl_rare(me)->jsdoc_overloads[i].callable->function;
        Span full = overload->full_span;
        Span tag_name = span_valid(full) && full.size > 1 ?
            span_make_in(full, full.buf + 1, full.buf + full.size) : full;
        if (!overload->ret_explicit && !my is_constructor && compiler_options()->no_implicit_any && span_valid(tag_name) &&
            span_valid(name)
        ) {
            // (tsgo's reparsed overload is a declaration without a body: reportImplicitAny's TS7010 with its name)
            error_ctx_error_d(errs, tag_name, 7010, "'", name, "', which lacks return-type annotation, implicitly has an 'any' return type.");
        }
        overloads[count] = overload;
        names[count++] = tag_name;
    }
    if (count && span_valid(name)) ty_check_overloads_compatible(current_checker, me, name, overloads, names, count, errs);
}

void function_impl_check_unused_renamings(FunctionImpl const* me, ErrorCtx* errs) {
    if (my has_body || !span_source(my full_span) || span_source(my full_span)->is_declaration) return;
    for_each (i, vec_function_arg, &my args) binding_check_unused_renamings(me, &i->initializer.binding, i, errs);
}

void function_impl_typecheck_signature(FunctionImpl const* me, bool is_type, ErrorCtx* errs) {
    if (!my has_body && span_source(my full_span) && !span_source(my full_span)->is_declaration) {
        for_each (i, vec_function_arg, &my args) binding_check_unused_renamings(me, &i->initializer.binding, i, errs);
    }
    generic_typecheck(&my generic, errs);
    function_impl_check_use_strict_parameters(me, errs);
    bool grammar_error = function_impl_check_grammar_parameters(me, errs);
    if (!grammar_error && my accessor && span_valid(function_impl_member_name(me))) {
        grammar_error = function_impl_check_grammar_accessor(me, errs);
    }
    if (!grammar_error && span_valid(function_impl_rare(me)->asterisk)) {
        // tsc's checkGrammarForGenerator()
        if (my ambient) {
            error_ctx_grammar_error_d(errs, function_impl_rare(me)->asterisk, 1221, "Generators are not allowed in an ambient context.");
        } else if (!my has_body) {
            error_ctx_grammar_error_d(errs, function_impl_rare(me)->asterisk, 1222, "An overload signature cannot be declared as a generator.");
        }
    }

    for_each (i, vec_function_arg, &my args) {
        bool has_initializer = initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY;
        bool is_pattern = i->initializer.binding.tag == BINDING_ARRAY || i->initializer.binding.tag == BINDING_OBJECT;
        if (i->is_parameter_property && is_pattern) {
            // (tsc's checkGrammarModifiers)
            error_ctx_grammar_error_d(errs, i->span, 1187, "A parameter property may not be declared using a binding pattern.");
        } else if (i->is_parameter_property && i->rest) {
            error_ctx_grammar_error_d(errs, i->span, 1317, "A parameter property cannot be declared using a rest parameter.");
        }
        if (i->is_parameter_property && compiler_options()->erasable_syntax_only) { // (tsc's checkParameter)
            error_ctx_error_d(errs, i->span, 1294, "This syntax is not allowed when 'erasableSyntaxOnly' is enabled.");
        }
        if (i->is_parameter_property && !(my is_constructor && my has_body)) {
            error_ctx_error_d(errs, i->span, 2369, "A parameter property is only allowed in a constructor implementation.");
        }
        if (i->is_parameter_property && my is_constructor) {
            ty_check_parameter_property_redeclaration(current_checker, function_impl_rare(me)->constructed_class, i, errs);
        }
        if (i->is_parameter_property && my is_constructor && i->initializer.binding.tag == BINDING_IDENTIFIER &&
            span_equals(i->initializer.binding.content_span, "constructor")
        ) {
            error_ctx_error_d(errs, i->initializer.binding.content_span, 2398,
                "'constructor' cannot be used as a parameter property name.");
        }
        if (is_this_parameter(i) && !my jsdoc_type && !i->jsdoc_this) {
            // (tsc's checkParameter; a `@this` tag's parameter is checked by the parser)
            if (i_index != 0) error_ctx_error_d(errs, i->span, 2680, "A 'this' parameter must be the first parameter.");
            if (my is_constructor) error_ctx_error_d(errs, i->span, 2681, "A constructor cannot have a 'this' parameter.");
            if (function_impl_is_arrow(me) && !is_type) {
                error_ctx_error_d(errs, i->span, 2730, "An arrow function cannot have a 'this' parameter.");
            }
            if (my accessor) {
                error_ctx_error_d(errs, i->span, 2784, "'get' and 'set' accessors cannot declare 'this' parameters.");
            }
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
        if ((is_type || !my has_body) && is_pattern) report_pattern_initializers(&i->initializer.binding, errs);
        // (A type's signature has no other check of its parameters: the elements of a pattern have to exist too, tsc's
        // checkParameter -> checkBindingElement)
        if (is_type && is_pattern) {
            binding_typecheck_bodiless((Binding*)&i->initializer.binding, errs);
        }

        if (i->rest) {
            // `(...args: infer A) => any` is fine: the implied constraint of A is `unknown[]`
            Binding const* binding = &i->initializer.binding;
            Checker* checker = current_checker;
            Ty const* rest_type = ty_binding_type(checker, binding, (LowerCtx){ .generic = true });
            // (`...x?` is the symbol's type, with undefined: tsc's addOptionality)
            if (binding->optional_parameter && compiler_options()->strict_null_checks && !(rest_type->flags & TYF_ANY)) {
                rest_type = ty_union(checker, (Ty const*[]){ rest_type, ty_intrinsic(checker, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
            }
            // (A pattern's type is what the pattern implies, tsc's checkParameter doesn't check it)
            if (binding->tag == BINDING_IDENTIFIER && !ty_extends_array(checker, rest_type)) {
                error_ctx_error_d(errs, i->span, 2370, "A rest parameter must be of an array type.");
            }
        }
    }

    // (tsc's checkTypePredicate; TODO: TS1230 for a name bound by a parameter's binding pattern)
    TypePredicateNode const* predicate = my predicate;
    if (predicate && !predicate->jsdoc && (my accessor || my is_constructor)) {
        // (tsc's getTypePredicateParent: not accessors, constructor types or construct signatures)
        error_ctx_error_d(errs, predicate->span, 1228,
            "A type predicate is only allowed in return type position for functions and methods.");
        return;
    }
    if (predicate && predicate->has_type && !predicate->jsdoc) type_typecheck(&my predicate->type, errs);
    if (predicate && !predicate->is_this && !predicate->jsdoc && span_valid(predicate->name)) {
        if (predicate->param_index >= 0) {
            FunctionArg const* arg = vec_function_arg_at(&my args, (isize_t)predicate->param_index);
            if (arg->rest) {
                error_ctx_error_d(errs, predicate->name, 1229, "A type predicate cannot reference a rest parameter.");
            } else if (predicate->has_type && span_valid(predicate->type_span)) {
                Checker* checker = current_checker;
                Ty const* predicate_type = ty_from_node(checker, &predicate->type, (LowerCtx){ .generic = true });
                ty_check_type_predicate(checker, predicate_type,
                    ty_binding_type(checker, &arg->initializer.binding, (LowerCtx){ .generic = true }), predicate->type_span,
                    errs);
            }
        } else {
            bool in_pattern = false;
            for_each (i, vec_function_arg, &my args) {
                Binding const* binding = &i->initializer.binding;
                if ((binding->tag == BINDING_ARRAY || binding->tag == BINDING_OBJECT) && pattern_declares(binding, predicate->name)) {
                    error_ctx_error_d(errs, predicate->name, 1230, "A type predicate cannot reference element '", predicate->name,
                        "' in a binding pattern.");
                    in_pattern = true;
                    break;
                }
            }
            if (!in_pattern) {
                error_ctx_error_d(errs, predicate->name, 1225, "Cannot find parameter '", predicate->name, "'.");
            }
        }
    }
}

// tsc's checkIfTypePredicateVariableIsDeclaredInBindingPattern: an element of the pattern (or a nested one) is named
static bool pattern_declares(Binding const* pattern, Span name) {
    if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) {
            if (i->tag == BINDING_IDENTIFIER && csview_equals_sv(span_sv(i->content_span), span_sv(name))) return true;
            if ((i->tag == BINDING_ARRAY || i->tag == BINDING_OBJECT) && pattern_declares(i, name)) return true;
        }
    } else if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) {
            Binding const* target = &i->target;
            if (target->tag == BINDING_IDENTIFIER && csview_equals_sv(span_sv(target->content_span), span_sv(name))) return true;
            if ((target->tag == BINDING_ARRAY || target->tag == BINDING_OBJECT) && pattern_declares(target, name)) return true;
        }
    }
    return false;
}

// tsc's maybeTypeOfKind(type, Void): through unions and intersections, `void & { brand: void }` (tsc's __String) too
static bool ty_maybe_void(Ty const* type) {
    if (type->flags & TYF_VOID) return true;
    if (type->kind != TY_UNION && type->kind != TY_INTERSECTION) return false;
    for (u32 i = 0; i < type->composite.types.len; i++) {
        if (ty_maybe_void(type->composite.types.data[i])) return true;
    }
    return false;
}

bool function_impl_return_void_any_or_undefined(FunctionImpl const* me, Ty const* return_type) {
    Ty const* unwrapped = ty_unwrap_return_type(current_checker, me, return_type);
    return unwrapped && (ty_maybe_void(unwrapped) || unwrapped->flags & (TYF_ANY | TYF_UNDEFINED));
}

FunctionImpl const* function_impl_jsdoc_tag_signature(FunctionImpl const* me) {
    TypeNode const* tag = function_impl_rare(me)->jsdoc_signature_type;
    if (!tag) return nullptr;
    if (tag->tag == TYPE_CALLABLE) return tag->callable->function;
    if (tag->tag == TYPE_INTERFACE && vec_interface_member_size(&tag->interface.members) == 1 &&
        tag->interface.members.data[0].key.tag == INTERFACE_KEY_CALLABLE && tag->interface.members.data[0].type.tag == TYPE_CALLABLE
    ) {
        return tag->interface.members.data[0].type.callable->function;
    }
    return nullptr;
}

// tsc: checkAllCodePathsInNonVoidFunctionReturnOrThrow(). Only when it is certain that control can fall off the end
// (ty_function_end_reachability())
static void function_impl_check_end(FunctionImpl* me, ErrorCtx* errs) {
    if (!my has_body || my is_constructor || my accessor == 2) return;
    Checker* checker = current_checker;
    if (ty_function_end_reachability(checker, me) != FLOW_REACH_YES) return;

    // What tsc reports a function-like node at: its name, the head of an arrow function, else its first token
    Span name = span_valid(my name) ? my name : function_impl_member_name(me);
    Span at = my ret_explicit ? function_impl_ret_span(me) : function_error_span(me); // (A getter's `@type` too: tsgo's reparsed Type)

    if (my accessor == 1 && !function_impl_has_explicit_return(me)) {
        // (tsc's checkAccessorDeclaration; checkAllCodePathsInNonVoidFunctionReturnOrThrow still runs after it)
        if (span_valid(name)) error_ctx_error_d(errs, name, 2378, "A 'get' accessor must return a value.");
    }
    Ty const* type = nullptr;
    if (my ret_explicit) {
        // (tsc's unwrapReturnType: an async function's is what it awaits to, error if that's nothing)
        type = ty_unwrapped_return_type(checker, me);
        if (!type && my is_async && !my is_generator) type = ty_intrinsic(checker, TY_ANY);
        if (type && (ty_maybe_void(type) || type->flags & (TYF_ANY | TYF_UNDEFINED))) return;
    } else if (my accessor == 1 && function_impl_setter_type(me)) {
        // A getter's is its setter's parameter annotation (tsc's getReturnTypeFromAnnotation: getAnnotatedAccessorType)
        type = ty_from_node(checker, function_impl_setter_type(me), (LowerCtx){ .generic = true });
        if (ty_maybe_void(type) || type->flags & (TYF_ANY | TYF_UNDEFINED)) return;
    } else if (function_impl_rare(me)->jsdoc_signature_type && !my is_generator) {
        // A JSDoc `@type` signature's return type is the function's (tsc's getReturnTypeFromAnnotation). An async
        // function's is what it awaits to (unwrapReturnType)
        type = ty_jsdoc_tag_return_type(checker, me);
        if (type && my is_async) {
            type = ty_unwrap_return_type(checker, me, type);
            if (!type) type = ty_intrinsic(checker, TY_ANY);
        }
        if (type && (ty_maybe_void(type) || type->flags & (TYF_ANY | TYF_UNDEFINED))) return;
        // (At the whole tag type: tsgo's errorNode is the function's FullSignature)
        if (span_valid(function_impl_rare(me)->jsdoc_signature_span)) at = function_impl_rare(me)->jsdoc_signature_span;
    }
    if (type && type->flags & TYF_NEVER) {
        error_ctx_error_d(errs, at, 2534, "A function returning 'never' cannot have a reachable end point.");
    } else if (type && !function_impl_has_explicit_return(me)) {
        error_ctx_error_d(errs, at, 2355,
            "A function whose declared type is neither 'undefined', 'void', nor 'any' must return a value.");
    } else if (type && compiler_options()->strict_null_checks &&
        !ty_is_related(checker, ty_intrinsic(checker, TY_UNDEFINED), type, TY_RELATION_ASSIGNABLE)
    ) {
        error_ctx_error_d(errs, at, 2366,
            "Function lacks ending return statement and return type does not include 'undefined'.");
    } else if (compiler_options()->no_implicit_returns) {
        if (!type) {
            if (!function_impl_has_explicit_return(me)) return;
            if (function_impl_return_void_any_or_undefined(me, ty_function_return_type(checker, me))) return;
        }
        error_ctx_error_d(errs, at, 7030, "Not all code paths return a value.");
    }
}

Span function_error_span(FunctionImpl const* me) {
    if (span_valid(my name)) return my name;
    if (span_valid(function_impl_member_name(me))) return function_impl_member_name(me);
    // (An anonymous function expression's assigned name: tsgo's GetNameOfDeclaration)
    if (span_valid(function_impl_rare(me)->assigned_name)) return function_impl_rare(me)->assigned_name;
    if (function_impl_is_arrow(me)) {
        // tsc's getErrorSpanForArrowFunction: a block body that spans lines (from right after the `=>`) makes it the
        // line of the `=>`
        Span ret = my full_span;
        if (!my expression_body) {
            char const* end = span_end_ptr(ret);
            char const* line_end = span_end_ptr(function_impl_arrow_span(me));
            while (line_end < end && *line_end != '\n' && *line_end != '\r') line_end++;
            if (line_end < end) ret.size = line_end - ret.buf;
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
}

void function_impl_check_implicit_any_return(FunctionImpl const* me, Span name, Span at, ErrorCtx* errs) {
    if (my private_ambient || my has_body || my ret_explicit || my accessor || my is_constructor || !span_valid(name)) return;
    if (!compiler_options()->no_implicit_any || (span_source(name) && span_source(name)->is_js)) return;
    error_ctx_error_d(errs, at, 7010, "'", span_name_text(name), "', which lacks return-type annotation, implicitly has an 'any' return type.");
}

// tsc's getReturnTypeOfSignature when the inferred return type references itself: TS7023 at the name (a function
// expression's is what it's assigned to, getNameOfDeclaration), TS7024 without one
static void function_check_circular_return(FunctionImpl const* me, ErrorCtx* errs) {
    if (!compiler_options()->no_implicit_any || !my has_body || my ret_explicit || function_impl_rare(me)->jsdoc_signature_type) return;
    if (!ty_function_return_circular(current_checker, me)) return;
    Span name = span_valid(my name) ? my name : span_valid(function_impl_member_name(me)) ? function_impl_member_name(me) : function_impl_variable_name(me);
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
    // (A JSDoc `@type` signature types every parameter, any past its own: tsgo's getParameterTypeOfFullSignature)
    if (function_impl_rare(me)->jsdoc_signature_type && ty_jsdoc_tag_return_type(current_checker, me)) return;
    for_each (i, vec_function_arg, &my args) {
        if (i->initializer.binding.has_explicit_type || initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY) continue;
        if (function_impl_rare(me)->jsdoc_signature_type) ty_function_apply_jsdoc_type(current_checker, me, nullptr);
        if (initialized_binding_getter(&i->initializer) || ty_contextual_parameter_type(current_checker, &i->initializer)) continue;
        if (i->name_is_type && my is_type && !my is_constructor) {
            error_ctx_error_d(errs, i->span, 7051, "Parameter has a name but no type. Did you mean 'arg", (u32)i_index, ": ",
                i->initializer.binding.content_span, i->rest ? "[]" : "", "'?");
            continue;
        }
        if (i->rest && i->initializer.binding.tag == BINDING_IDENTIFIER) {
            error_ctx_error_d(errs, i->span, 7019, "Rest parameter '", span_name_text(i->initializer.binding.content_span),
                "' implicitly has an 'any[]' type.");
        } else {
            binding_add_implicit_any_type_errors(&i->initializer.binding, i->span, !my has_body, errs);
        }
    }
}

// tsc's reportErrorsFromWidening for an inferred return type: null and undefined in it widen to any without
// strictNullChecks (`return null`, `return [null]`, `return { a: null }`). TS7018 at the object literal properties
// that do, else TS7010 (TS7011 for an unnamed function expression) with the widened type. A contextually typed
// function only if the contextual return type is generic (shouldReportErrorsFromWideningWithContextualSignature)
// tsc's reportErrorsFromWidening: inside the type if it can (object literal properties), else at the function
// (WideningKind.GeneratorYield for a generator's yields, else FunctionReturn)
static void report_widened_return(FunctionImpl const* me, Span name, Ty const* unwidened, bool yield, ErrorCtx* errs) {
    if (binding_report_widening_errors_in_type(unwidened, errs)) return;
    cstr type = ty_to_cstr(current_checker, ty_widened_nullable(current_checker, ty_widened_objects(current_checker, unwidened)));
    if (yield && span_valid(name)) {
        error_ctx_error_d(errs, name, 7055, "'", span_name_text(name), "', which lacks return-type annotation, implicitly has an '", type,
            "' yield type.");
    } else if (yield) {
        error_ctx_error_d(errs, function_error_span(me), 7025, "Generator implicitly has yield type '", type,
            "'. Consider supplying a return type annotation.");
    } else if (span_valid(name)) {
        error_ctx_error_d(errs, name, 7010, "'", span_name_text(name), "', which lacks return-type annotation, implicitly has an '", type,
            "' return type.");
    } else {
        error_ctx_error_d(errs, function_error_span(me), 7011,
            "Function expression, which lacks return-type annotation, implicitly has an '", type, "' return type.");
    }
    cstr_drop(&type);
}

static void function_check_widened_return(FunctionImpl const* me, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    if (!options->no_implicit_any || options->strict_null_checks || my ret_explicit || !my has_body) return;
    // (A getter too, unless its type is its setter's parameter annotation: tsc's reportImplicitAny for a GetAccessor)
    if (my is_constructor || my accessor == 2 || (my accessor && function_impl_setter_type(me)) || my jsdoc_typed) return;
    Checker* checker = current_checker;
    Span name = span_valid(my name) ? my name : function_impl_member_name(me);
    // (A generator's yields, then its returns: tsc's getReturnTypeFromBody)
    Ty const* unwidened = ty_function_return_before_widening(checker, me);
    // Its contextual signature is its own (an argument whose inferred type parameter is the function itself):
    // tsc's shouldReportErrorsFromWideningWithContextualSignature asks for that signature's return type, which is the
    // one being resolved, a circularity (TS7023/TS7024, and the return type is any)
    if (unwidened && my is_expression && ty_function_context_is_self(checker, me)) {
        Span circular = span_valid(name) ? name : function_impl_variable_name(me);
        if (span_valid(circular)) {
            error_ctx_error_d(errs, circular, 7023, "'", circular, "' implicitly has return type 'any' because it does not have a return "
                "type annotation and is referenced directly or indirectly in one of its return expressions.");
        } else {
            error_ctx_error_d(errs, function_error_span(me), 7024, "Function implicitly has return type 'any' because it does not "
                "have a return type annotation and is referenced directly or indirectly in one of its return expressions.");
        }
        return;
    }
    if (unwidened && (!my is_expression || ty_function_widening_reportable(checker, me, my is_generator))) {
        report_widened_return(me, name, unwidened, my is_generator, errs);
    }
    Ty const* returned = my is_generator ? ty_generator_return_before_widening(checker, me) : nullptr;
    if (returned && (!my is_expression || ty_function_widening_reportable(checker, me, false))) {
        report_widened_return(me, name, returned, false, errs);
    }
}

// The parameters a call has to pass (tsc's isAritySmaller of the target): up to the first optional one, `this` aside
static u32 jsdoc_required_params(FunctionImpl const* me) {
    u32 count = 0;
    for_each (i, vec_function_arg, &my args) {
        Binding const* binding = &i->initializer.binding;
        if (i->jsdoc_this || (!i_index && binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this"))) continue;
        if (i->rest || i->explicit_optional || initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY) break;
        count++;
    }
    return count;
}

void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs) {
    function_check_emit_helpers(me, errs);
    function_impl_check_implicit_any_parameters(me, errs);
    // (Type parameters first, then the parameters: tsc's checkSignatureDeclaration)
    function_impl_typecheck_signature(me, false, errs);
    // (The parameter decorators of a class member are the class's to check: they are calls with synthetic arguments,
    // which is what gives them their contextual type)
    bool class_member = !my is_expression && (span_valid(function_impl_member_name(me)) || function_impl_rare(me)->constructed_class);
    for_each (i, vec_function_arg, &my args) {
        if (!class_member && vec_expr_size(function_arg_decorators(i))) {
            // A parameter of a function or object literal member can't be decorated (tsc's checkGrammarModifiers,
            // nodeCanBeDecorated: TS1206 at the first decorator's `@`)
            if (span_valid(i->span)) {
                Span at = i->span;
                span_set_sv(&at, csview_from_n(span_start_ptr(i->span), 1));
                error_ctx_grammar_error_d(errs, at, 1206, "Decorators are not valid here.");
            }
            // (Never decorated: tsc's checkDecorators doesn't look at them)
            ErrorCtx ignored = error_ctx_scratch(errs);
            for_each (decorator, vec_expr, function_arg_decorators(i)) {
                expr_typecheck(decorator, &ignored);
            }
            error_ctx_drop(&ignored);
        }
        // (The type of a JSDoc `@this {T}` isn't checked: tsc's checkJSDocThisTag only looks at its host)
        if (i->jsdoc_this) continue;
        // (An overload's or ambient function's initializer is TS2371 and nothing else: tsc's checkVariableLikeDeclaration
        // returns there, after the annotation)
        if (!my has_body && initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY) {
            if (i->initializer.binding.has_explicit_type) type_typecheck(binding_type(&i->initializer.binding), errs);
            // (A pattern's elements are checked before that: checkSourceElements of the elements, whose types look at
            // the initializer under a written type, getBindingElementTypeFromParentType)
            Binding* binding = (Binding*)&i->initializer.binding;
            if ((binding->tag == BINDING_ARRAY || binding->tag == BINDING_OBJECT) && !my ambient) {
                binding_typecheck_bodiless(binding, errs);
            }
            continue;
        }
        initialized_binding_typecheck_parameter(&i->initializer, i->span, !my has_body, errs);
        if ((i->initializer.binding.tag == BINDING_ARRAY || i->initializer.binding.tag == BINDING_OBJECT) &&
            initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY &&
            !i->initializer.binding.has_explicit_type
        ) {
            binding_add_padded_implicit_any_errors(&i->initializer.binding,
                ty_expr_type(current_checker, initialized_binding_value(&i->initializer)), errs);
        }
    }

    if (my ret_explicit) type_typecheck(&my ret, errs);
    ty_check_async_return_annotation(current_checker, me, errs);
    for (u32 i = 0; i < function_impl_rare(me)->jsdoc_unmatched_count; i++) type_typecheck(&function_impl_rare(me)->jsdoc_unmatched[i].type, errs);
    // A function's `@type` needs a call signature that takes its parameters (tsgo's FullSignature check in
    // checkFunctionOrMethodDeclaration and checkFunctionExpressionOrObjectLiteralMethod; not an accessor's)
    if (span_valid(function_impl_rare(me)->jsdoc_signature_span) && my has_body && !my accessor) {
        bool mismatch = my jsdoc_signature_mismatch;
        if (!mismatch && function_impl_rare(me)->jsdoc_signature_type) {
            mismatch = !ty_has_call_signature_for_arity(current_checker, ty_of_node(function_impl_rare(me)->jsdoc_signature_type), jsdoc_required_params(me));
        }
        if (mismatch) {
            error_ctx_error_d(errs, function_impl_rare(me)->jsdoc_signature_span, 8030,
                "A JSDoc '@type' tag on a function must have a signature with the correct number of arguments.");
        }
    }
    if (!my is_constructor) function_impl_check_jsdoc_overloads(me, my name, errs);
    // (tsc's checkUnmatchedJSDocParameters: a body that uses `arguments` only has its last tag checked, which could
    // stand for it with an array type)
    u32 candidate = function_impl_rare(me)->jsdoc_arguments_candidate;
    if (my uses_arguments && candidate && candidate - 1 < function_impl_rare(me)->jsdoc_unmatched_count) {
        JsDocUnmatchedParam const* last = &function_impl_rare(me)->jsdoc_unmatched[candidate - 1];
        // (A `{...T}` is `T[]`: tsgo's getTypeFromTypeNode of a JSDocVariadicType)
        bool array = last->variadic || ty_is_array(current_checker, ty_of_node(&last->type));
        if (!array) {
            error_ctx_error_d(errs, last->name, 8029, "JSDoc '@param' tag has name '", last->name,
                "', but there is no parameter with that name. It would match 'arguments' if it had an array type.");
        }
    }
    for (u32 i = 0; i < function_impl_rare(me)->jsdoc_param_issue_count && !my uses_arguments; i++) {
        JsDocParamIssue const* issue = &function_impl_rare(me)->jsdoc_param_issues[i];
        if (span_valid(issue->left)) {
            // (The names as tsc's scanner has them: escapes decoded)
            error_ctx_error_d(errs, issue->name, 8032, "Qualified name '", name_cooked(span_sv(issue->name)),
                "' is not allowed without a leading '@param {object} ", name_cooked(span_sv(issue->left)), "'.");
        } else {
            error_ctx_error_d(errs, issue->name, 8024, "JSDoc '@param' tag has name '", name_cooked(span_sv(issue->name)),
                "', but there is no parameter with that name.");
        }
    }

    if (my newline_before_arrow) {
        error_ctx_error_d(errs, function_impl_arrow_span(me), 1200, "Line terminator not permitted before arrow.");
    }

    // A function expression's body is checked after the statements of its file (tsc's checkNodeDeferred): its
    // signature is checked where it is, which is what the code around it sees
    if (my is_expression && current_checker->defer_checks) {
        // (One with a contextual signature gets its return type from its body where it is: tsc's
        // contextuallyCheckFunctionExpressionOrObjectLiteralMethod)
        if (!my ret_explicit && my has_body && ty_function_assigned_contextual_signature(current_checker, me)) {
            ty_function_return_type(current_checker, me);
        }
        checker_defer_function_body(current_checker, me);
    } else {
        function_impl_typecheck_body(me, errs);
    }
}

// What function_impl_typecheck() checks of a function's body (tsc's checkFunctionExpressionOrObjectLiteralMethodDeferred
// for a function expression)
void function_impl_typecheck_body(FunctionImpl* me, ErrorCtx* errs) {
    // tsc's checkGrammarStatementInAmbientContext for the body, which then covers the statements directly in it
    if (my ambient && my has_body && !my expression_body && span_source(my full_span) && !span_source(my full_span)->has_parse_errors) {
        // (The body's `{`: the first after the parameters and the return type)
        char const* at = span_valid(function_impl_ret_span(me)) && span_end_ptr(function_impl_ret_span(me)) > span_end_ptr(function_impl_args_span(me)) ?
            span_end_ptr(function_impl_ret_span(me)) : span_end_ptr(function_impl_args_span(me));
        char const* end = span_end_ptr(my full_span);
        while (at && at < end && *at != '{') at++;
        Span body = at && at < end ? block_first_token(span_make_in(my full_span, at, end)) : span_null;
        if (span_valid(body)) error_ctx_error_d(errs, body, 1183, "An implementation cannot be declared in ambient contexts.");
    }
    // (A function expression's return type is asked for before its body is checked, whatever the body says: tsc's
    // checkFunctionExpressionOrObjectLiteralMethodDeferred, where a missing Promise value is reported)
    if (my is_expression && my has_body && !my ret_explicit && !my is_generator) ty_function_return_type(current_checker, me);
    // (A declaration's or generator's return type is only asked for after its body is checked: tsc's
    // checkFunctionOrMethodDeclaration)
    if (!my is_generator && my is_expression) function_check_widened_return(me, errs);
    // (A JSDoc `@type` signature's return type is the function's, what it returns is checked against it: tsc's
    // getReturnTypeFromAnnotation)
    Ty const* tagged_return = !my ret_explicit && !my is_generator &&
        function_impl_rare(me)->jsdoc_signature_type ? ty_jsdoc_tag_return_type(current_checker, me) : nullptr;
    // (An async function's returns are checked against what it awaits to)
    if (tagged_return && my is_async) tagged_return = ty_unwrap_return_type(current_checker, me, tagged_return);
    BlockTypecheckCtx ctx = {
        .ambient = my ambient,
        .ambient_reported = true,
        .in_function = true,
        // TODO: returns in async functions and generators aren't checked, the declared type wraps what's returned
        .specified_return = my ret_explicit && !my is_async && !my is_generator ? &my ret :
            function_impl_setter_type(me) ? function_impl_setter_type(me) : &type_any,
        .constructed_class = my is_constructor ? function_impl_rare(me)->constructed_class : nullptr,
        .unwrapped_return = my ret_explicit && (my is_async || my is_generator) ? ty_unwrapped_return_type(current_checker, me) :
            tagged_return,
        .await_returned = my is_async,
        .function = me,
        .in_setter = my accessor == 2,
    };
    if (my ret_explicit && my is_generator && my has_body) ty_check_generator_annotation(current_checker, me, errs);
    Ty const* outer_context = function_impl_enter_return_context(me);
    // (tsc checks a function expression's body later, outside any unreachable code around it)
    bool was_within_unreachable = current_checker->within_unreachable;
    if (my is_expression) current_checker->within_unreachable = false;
    bool flow_disabled = current_checker->flow_analysis_disabled;
    block_typecheck(function_impl_body_mut(me), ctx, errs);
    current_checker->flow_analysis_disabled = flow_disabled;
    current_checker->within_unreachable = was_within_unreachable;
    current_checker->return_context = outer_context;
    function_impl_check_end(me, errs);
    // A generator declaration or class method without a return annotation: its return type, which is where a missing
    // global Generator/IterableIterator is reported (tsc's checkFunctionOrMethodDeclaration asks for it for that)
    if (my is_generator && !my is_expression && my has_body && !my ret_explicit) ty_function_return_type(current_checker, me);
    if (my is_generator || !my is_expression) function_check_widened_return(me, errs);
    function_check_circular_return(me, errs);
}
