#include "binding.h"
#include "port_statement.h"
#include "flow.h"
#include "scope.h"
#include "several_types.h"
#include "display.h"
#include "error.h"
#include "ty/ty.h"
#include "unused.h"
#include "source_file.h"
#include "function_impl.h"

static void span_to_offsets(Binding const* me, Span span, u32* start, u32* length) {
    panic_if(span.source != my content_span.source, "a binding's span in another file than its name");
    *start = (u32)(span.sv.buf - source_file_start(span.source));
    *length = (u32)span.sv.size;
}

Span binding_explicit_type_span(Binding const* me) {
    if (!my has_explicit_type_span) return span_null;
    char const* start = source_file_start(my content_span.source) + my explicit_type_start;
    return (Span){ .source = my content_span.source, .sv = { start, my explicit_type_length } };
}

Span binding_element_span(Binding const* me) {
    if (!my has_element_span) return span_null;
    char const* start = source_file_start(my content_span.source) + my element_start;
    return (Span){ .source = my content_span.source, .sv = { start, my element_length } };
}

void binding_set_explicit_type_span(Binding* me, Span span) {
    my has_explicit_type_span = span_valid(span);
    if (my has_explicit_type_span) span_to_offsets(me, span, &my explicit_type_start, &my explicit_type_length);
}

void binding_set_element_span(Binding* me, Span span) {
    my has_element_span = span_valid(span);
    if (my has_element_span) span_to_offsets(me, span, &my element_start, &my element_length);
}

void binding_drop(Binding* me) {
    switch (my tag) {
        case BINDING_NOOP: break;
        case BINDING_IDENTIFIER: break;
        case BINDING_ARRAY: vec_binding_drop(&my array); break;
        case BINDING_OBJECT: vec_object_binding_item_drop(&my object); break;
    }
    free(my access_site);
    type_drop(&my explicit_type);
    if (my default_value) {
        expr_drop(my default_value);
        free(my default_value);
    }
}

void binding_add_names(Binding* me, LazyType* source, Scope* scope, BindingAddNamesProps props) {
    my source = source;
    my ambient = props.is_ambient || scope->ambient;

    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            my is_parameter = props.is_function_arg;
            // TODO: somehow abstract literals on the exported value only (see defaultNamedExportWithType1)
            scope_init_declaration(scope, &my symbol, (DeclProps){
                .name = my content_span,
                .full = span_valid(props.full) ? props.full : my content_span,
                .exported = props.exported,
                .kind = SYMBOL_DECL_VARIABLE,
                .node = me,
                .is_parameter = props.is_function_arg,
                .function_scoped = props.function_scoped,
                .is_const = props.is_const,
                .is_ambient = props.is_ambient,
            });
            break;

        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) {
                props.full = i->has_element_span ? binding_element_span(i) : i->content_span;
                i->parent = me;
                i->parent_index = (u32)i_index;
                binding_add_names(i, nullptr, scope, props);
            }
            break;

        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                // The element starts at its property name (`{ x: a }` declares `a` at `x`)
                props.full = i->target.rest ? binding_element_span(&i->target) :
                    span_valid(i->source_span) && span_start_ptr(i->source_span) < span_start_ptr(i->target.content_span) ?
                        span_between(i->source_span, i->target.content_span) : i->target.content_span;
                i->target.parent = me;
                i->target.parent_item = i;
                binding_add_names(&i->target, nullptr, scope, props);
            }
            break;
    }
}

void binding_collect_names(Binding const* me, VecSpan* out) {
    switch (my tag) {
        case BINDING_NOOP: break;
        case BINDING_IDENTIFIER: vec_span_push(out, my content_span); break;
        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) binding_collect_names(i, out);
            break;
        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) binding_collect_names(&i->target, out);
            break;
    }
}

// tsc's checkVarDeclaredNamesNotShadowed(): a `var` hoists out of the blocks it is written in, past any let/const of
// the same name one of them declares (the let/const of the function or file itself would be a duplicate declaration)
static void binding_check_var_not_shadowed(Binding const* me, Scope const* names, ErrorCtx* errs) {
    Symbol const* symbol = my symbol;
    if (!symbol || !(symbol->flags & SYMBOL_FUNCTION_SCOPED_VARIABLE) || symbol->is_parameter) return;
    csview name = my content_span.sv;
    for (Scope const* scope = names; scope && scope->kind == SCOPE_BLOCK; scope = scope->parent) {
        Symbol const* found = symbol_table_get(&scope->locals, name);
        if (!found || found == symbol || !(found->flags & SYMBOL_VARIABLE)) continue;
        // A catch variable is block scoped too, but isn't a let/const (the root of its pattern has no source)
        Binding const* decl = symbol_find_node(found, SYMBOL_DECL_VARIABLE);
        while (decl && decl->parent) decl = decl->parent;
        if ((found->flags & SYMBOL_BLOCK_SCOPED_VARIABLE) && decl && decl->source) {
            error_ctx_error_d(errs, my content_span, 2481, "Cannot initialize outer scoped variable '", name,
                "' in the same scope as block scoped declaration '", name, "'.");
        }
        return;
    }
}

void binding_lookup_names(Binding* me, Scope* names, ErrorCtx* errs) {
    Binding const* outer_root = names->flow ? names->flow->binding_root : nullptr;
    if (names->flow && !my parent) names->flow->binding_root = me;
    // The type, then the default, then the pattern: a default is evaluated before the elements it is destructured
    // into (tsc's bindParameterFlow / bindBindingElementFlow)
    if (my has_explicit_type) {
        // A JSDoc `@param` type is in the function's comment, which doesn't see its parameters as values (the parameter
        // list's own scope declares nothing: flagging it while the type is walked is a scope of its own, for free)
        bool jsdoc_parameter_type = my explicit_type.from_jsdoc && names->parameters && !names->jsdoc_parameter_type;
        if (jsdoc_parameter_type) names->jsdoc_parameter_type = true;
        type_lookup_names(&my explicit_type, names, errs);
        if (jsdoc_parameter_type) names->jsdoc_parameter_type = false;
    }
    if (my default_value) {
        // In a parameter's pattern the default is the element's own (tsc's associatedDeclarationForContainingInitializer)
        Binding const* saved = names->flow ? names->flow->parameter_initializing : nullptr;
        if (saved) names->flow->parameter_initializing = me;
        expr_lookup_names_initializer(my default_value, names, errs);
        if (saved) names->flow->parameter_initializing = saved;
    }
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            // The flow container the variable belongs to: elsewhere it is assumed to be initialized
            // (Only the first declaring file's walk writes it: others may be running on other threads, plan 45)
            if (my symbol && names->flow && !symbol_first_declared_elsewhere(my symbol, names->file) && !my symbol->flow_start) {
                my symbol->flow_start = names->flow->container_start;
            }
            binding_check_var_not_shadowed(me, names, errs);
            break;

        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) {
                binding_lookup_names(i, names, errs);
            }
            break;

        case BINDING_OBJECT:
            free(my access_site);
            my access_site = scope_access_site(names);
            for_each (i, vec_object_binding_item, &my object) {
                if (i->computed_key) {
                    expr_lookup_names(i->computed_key, names, errs);
                }
                binding_lookup_names(&i->target, names, errs);
            }
            break;
    }

    if (names->flow && !my parent) names->flow->binding_root = outer_root;
}

// The default of a parameter or of an element of a pattern is only evaluated sometimes (tsc: bindInitializer())
void expr_lookup_names_initializer(Expr* me, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    FlowNode* entry = flow->current;
    expr_lookup_names(me, names, errs);
    if (flow_is_unreachable(entry) || entry == flow->current) return;
    FlowNode* exit = flow_new_branch_label(flow);
    flow_add_antecedent(flow, exit, entry);
    flow_add_antecedent(flow, exit, flow->current);
    flow_set_current(flow, flow_finish_label(exit));
}

// Every variable a declaration with an initializer (or of a for-in/of) declares is assigned there
void binding_flow_assign(Binding const* me, Scope* names) {
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            flow_assign(names->flow, (FlowAssign){ .kind = FLOW_ASSIGN_DECLARATION, .binding = me });
            break;

        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) {
                binding_flow_assign(i, names);
            }
            break;

        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                binding_flow_assign(&i->target, names);
            }
            break;
    }
}

// tsc's getTypeFromBindingElement with reportErrors: an element with a default takes the default's type, so only the
// elements without one (and the patterns without one, element by element) are implicitly any
// tsc's checkVariableLikeDeclaration doesn't check a renaming (`{ a: b }`) in the parameters of a signature without a
// body at all (`bodiless`): only TS2842 is said about it
void binding_add_implicit_any_type_errors(Binding const* me, Span parameter, bool bodiless, ErrorCtx* errs) {
    bool toplevel = span_valid(parameter);
    if (!toplevel && my default_value) {
        // A default of null or undefined widens to any without strictNullChecks (tsc's reportErrorsFromWidening)
        if (my tag != BINDING_IDENTIFIER || compiler_options()->strict_null_checks) return;
        Ty const* type = ty_expr_type(current_checker, my default_value);
        if (!(type->flags & TYF_NULLABLE) || type->kind == TY_UNION) return;
    }
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            error_ctx_error_d(
                errs, toplevel ? parameter : my content_span, toplevel ? 7006 : 7031,
                toplevel ? "Parameter" : "Binding element", " '", my content_span, "' implicitly has an 'any' type."
            );
            break;

        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) {
                binding_add_implicit_any_type_errors(i, span_null, bodiless, errs);
            }
            break;

        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                bool renaming = i->target.tag == BINDING_IDENTIFIER && !i->target.rest &&
                    !span_eq(i->source_span, i->target.content_span);
                if (bodiless && renaming) continue;
                binding_add_implicit_any_type_errors(&i->target, span_null, bodiless, errs);
            }
            break;
    }
}

// Whether a pattern belongs to a parameter (tsc's walkUpBindingElementsAndPatterns + isParameter): its first variable
// says so, and a pattern without any has nothing to pad
bool binding_is_parameter(Binding const* pattern) {
    switch (pattern->tag) {
        case BINDING_NOOP: return false;
        case BINDING_IDENTIFIER: return pattern->symbol && pattern->symbol->is_parameter;
        case BINDING_ARRAY:
            for_each (i, vec_binding, &pattern->array) {
                if (i->tag != BINDING_NOOP) return binding_is_parameter(i);
            }
            return false;
        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &pattern->object) return binding_is_parameter(&i->target);
            return false;
    }
    return false;
}

void binding_add_padded_implicit_any_errors(Binding const* me, Ty const* initializer, ErrorCtx* errs) {
    if (my tag != BINDING_ARRAY || !compiler_options()->no_implicit_any) return;
    TupleShape const* shape = ty_tuple_shape(initializer);
    if (!shape) return;
    for (u32 i = 0; i < shape->arity; i++) {
        if (shape->flags[i] & TUPLE_ELEMENT_REST) return;
    }
    u32 len = (u32)vec_binding_size(&my array);
    for (u32 i = shape->arity; i < len; i++) {
        Binding const* element = vec_binding_at(&my array, i);
        if (element->tag == BINDING_NOOP || element->default_value || (i == len - 1 && element->rest)) continue;
        // (tsc's reportImplicitAny of a binding element: the name for an identifier, else the whole pattern)
        error_ctx_error_d(errs, element->content_span, 7031, "Binding element '", element->content_span,
            "' implicitly has an 'any' type.");
    }
}

// tsc's checkVariableLikeDeclaration for a declaration that isn't the symbol's first (`var x: A; var x: B`): the same
// type (TS2403)
static void check_subsequent_declaration(Binding const* me, ErrorCtx* errs) {
    if (!my content_span.sv.size) return; // (a missing name: recovery's, nothing is said about it)
    Symbol const* symbol = my symbol;
    SymbolDecl const* first = nullptr;
    // (The namespace an expando makes of a name isn't its value declaration: tsc's setValueDeclaration)
    for (u32 i = 0; i < symbol->decl_count && !first; i++) {
        if (symbol->decls[i].flags & SYMBOL_VALUE && symbol->decls[i].kind != SYMBOL_DECL_EXPANDO) first = &symbol->decls[i];
    }
    if (!first || first->node == me) return;
    // Not for a symbol a top-level assignment made a namespace (tsc: SymbolFlags.Assignment)
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind == SYMBOL_DECL_EXPANDO && span_valid(symbol->decls[i].full)) return;
    }
    Checker* checker = current_checker;
    Ty const* type = ty_type_of_symbol(checker, symbol);
    Ty const* declared = ty_binding_type(checker, me, (LowerCtx){ .generic = true });
    Ty const* error = ty_intrinsic(checker, TY_ERROR);
    Ty const* circular = ty_intrinsic(checker, TY_CIRCULAR);
    if (type == error || declared == error || declared == circular ||
        ty_is_related(checker, type, declared, TY_RELATION_IDENTITY)
    ) {
        return;
    }
    cstr type_str = ty_to_cstr(checker, type);
    cstr declared_str = ty_to_cstr(checker, declared);
    Error* e = error_ctx_error_d(errs, my content_span, 2403,
        "Subsequent variable declarations must have the same type.  Variable '", my content_span, "' must be of type '",
        type_str, "', but here has type '", declared_str, "'.");
    // (tsc's createDiagnosticForNode of the value declaration: a parameter's starts at its modifiers)
    Binding const* first_binding = first->kind == SYMBOL_DECL_VARIABLE ? first->node : nullptr;
    bool first_parameter = first_binding && first_binding->is_parameter && !first_binding->parent;
    Span first_at = first_parameter && span_valid(first->full) ? first->full : first->name;
    error_add_related_d(e, first_at, 6203, "'", my content_span, "' was also declared here.");
    c_drop(cstr, &type_str, &declared_str);
}

// tsc's hasTypeFacts(type, EQUndefined), roughly: whether a value of the type may be undefined
static bool possibly_undefined(Ty const* type) {
    u32 may = TYF_ANY | TYF_UNKNOWN | TYF_UNDEFINED | TYF_VOID | TYF_TYPE_PARAM | TYF_DEFERRED;
    if (type->kind == TY_UNION) {
        for (u32 i = 0; i < type->composite.types.len; i++) {
            if (type->composite.types.data[i]->flags & may) return true;
        }
        return false;
    }
    return (type->flags & may) != 0;
}

// The type a pattern's elements are taken from, as far as reporting what it lacks goes (tsc's
// getBindingElementTypeFromParentType): null when nothing is reported (any, or nothing an element could be missing from).
// An object pattern lacks every property of unknown (TS2339), an array pattern reports it once (TS2461).
// `initializer`: what the pattern's declaration is initialized with (a root's initializer, else the pattern's default)
static Ty const* element_parent_type(Binding const* me, Expr const* initializer) {
    Checker* checker = current_checker;
    Ty const* type = ty_binding_type(checker, me, (LowerCtx){ .generic = true });
    // (Not widened: `= null` is null, not any, without strictNullChecks. tsc's getTypeForBindingElementParent)
    if (my tag == BINDING_OBJECT && !my has_explicit_type && initializer && !(type->flags & TYF_NULLABLE)) {
        Ty const* assigned = ty_expr_type(checker, initializer);
        if (assigned->flags & TYF_NULLABLE) type = assigned;
    }
    if (type->flags & TYF_ANY || (type->flags & TYF_UNKNOWN && my tag == BINDING_ARRAY)) return nullptr;
    // (tsc takes the non-nullable part: `{}` without strictNullChecks, unknown itself with them)
    if (type->flags & TYF_UNKNOWN) return compiler_options()->strict_null_checks ? type : ty_empty_object_type(checker);
    if (my tag == BINDING_OBJECT) {
        // Null and undefined stay: `const { a } = null` or of a `T | undefined` is TS2339 on the whole type
        // (tsc's getBindingElementTypeFromParentType), except in an ambient parameter, or undefined when the
        // initializer can't be undefined
        if (!compiler_options()->strict_null_checks) {
            if (!(type->flags & TYF_NULLABLE)) type = ty_non_nullable(checker, type);
        } else if (my ambient && binding_is_parameter(me)) {
            type = ty_non_nullable(checker, type);
        } else if (initializer && !possibly_undefined(ty_expr_type(checker, initializer))) {
            type = ty_without_undefined(checker, type);
        }
        if (type->flags & TYF_NEVER) return nullptr;
        return type;
    }
    // (What can be null or undefined is reported about the whole pattern)
    type = ty_non_nullable(checker, type);
    if (type->flags & TYF_NEVER) return nullptr;
    return type;
}

// tsc's checkGrammarBindingElement: a rest element's initializer, at its `=`
static void binding_report_rest_initializer(Binding const* element, ErrorCtx* errs) {
    Span value = expr_outer_span(element->default_value);
    if (!span_valid(value)) return;
    char const* at = span_start_ptr(value);
    char const* start = span_start_ptr(element->content_span);
    while (at > start && at[-1] != '=') at--;
    if (at <= start) return;
    error_ctx_grammar_error_d(errs, span_make(value.source, at - 1, at), 1186, "A rest element cannot have an initializer.");
}

// tsc's checkGrammarBindingElement: a trailing comma after the rest element (checkGrammarForDisallowedTrailingComma)
static void binding_report_rest_trailing_comma(Binding const* element, ErrorCtx* errs) {
    Span extent = element->content_span;
    if (element->default_value) {
        Span value = expr_outer_span(element->default_value);
        if (span_valid(value) && span_valid(extent) && span_end_ptr(value) > span_end_ptr(extent)) {
            extent = span_make(extent.source, span_start_ptr(extent), span_end_ptr(value));
        }
    }
    Span comma = span_comma_after(extent);
    if (span_valid(comma)) {
        error_ctx_grammar_error_d(errs, comma, 1013, "A rest parameter or binding pattern may not have a trailing comma.");
    }
}

static void binding_typecheck_at(Binding* me, Expr const* initializer, Span span, bool assigned, bool type_checked, ErrorCtx* errs);

void binding_typecheck(Binding* me, Expr const* initializer, ErrorCtx* errs) {
    binding_typecheck_at(me, initializer, span_null, true, false, errs);
}

// A JavaScript `/** @type {T} */ const x = {}` whose members are expandos (`x.a = 1`) isn't checked against T (tsc's
// checkVariableLikeDeclaration: isJSObjectLiteralInitializer)
static bool is_js_expando_object_initializer(Binding const* me, Expr const* initializer) {
    if (!initializer || initializer->tag != EXPR_OBJECT || vec_object_expr_item_size(&initializer->object)) return false;
    if (my tag != BINDING_IDENTIFIER || !my symbol || !my symbol->exports || !my symbol->exports->count) return false;
    return my content_span.source && my content_span.source->is_js;
}

// `span`: where the initializer's check reports (a parameter's is the whole parameter, tsc's getErrorSpanForNode).
// An element of a parameter's pattern in the parameter's default as it is, not widened: tsc's
// getTypeForBindingElementParent reads the parameter's resolved type when there is one, else its unwidened declared
// type, which is what the usual order (the parameter checked before anything resolves its type) sees. So
// `function f([a] = [undefined])` has an `a` of undefined, which widens to any (reportErrorsFromWidening, TS7031).
// Only the pattern's own elements without a default of their own: nothing else needs it yet
static Ty const* parameter_element_before_widening(Binding const* me) {
    Binding const* pattern = my parent;
    if (pattern->parent || pattern->has_explicit_type || my default_value || compiler_options()->strict_null_checks ||
        !pattern->source || pattern->source->tag != LAZY_TYPE_INITIALIZED_BINDING || !binding_is_parameter(pattern)
    ) {
        return nullptr;
    }
    Expr const* initializer = initialized_binding_value(c_container_of(pattern, InitializedBinding, binding));
    if (initializer->tag == EXPR_IMPLICIT_ANY) return nullptr;
    Checker* checker = current_checker;
    Ty const* object = ty_expr_type(checker, initializer);
    Ty const* key;
    if (pattern->tag == BINDING_ARRAY) {
        Value index = value_number(my parent_index);
        key = ty_literal(checker, &index);
    } else {
        if (my parent_item->computed_key) return nullptr;
        key = ty_literal(checker, &my parent_item->source);
    }
    Ty const* ret = ty_access_reading(checker, object, key);
    return ret && (ret->xflags & TYX_CONTAINS_WIDENING) ? ret : nullptr;
}

// `assigned`: whether the root is assigned anything (not a variable or parameter without initializer)
// `type_checked`: the annotation was walked already
static void binding_typecheck_at(Binding* me, Expr const* initializer, Span span, bool assigned, bool type_checked, ErrorCtx* errs) {
    if (my has_explicit_type && !type_checked) type_typecheck(&my explicit_type, errs);
    // A pattern without elements is checked for null and undefined instead: the initializer's type, and the pattern's
    // (tsc's checkVariableLikeDeclaration: checkNonNullNonVoidType, both are one diagnostic if they say the same)
    bool empty_pattern = my tag == BINDING_OBJECT && !vec_object_binding_item_size(&my object);
    if (my tag == BINDING_ARRAY) {
        empty_pattern = true;
        for_each (i, vec_binding, &my array) empty_pattern &= i->tag == BINDING_NOOP;
    }
    bool strict_empty = empty_pattern && compiler_options()->strict_null_checks && !my ambient;
    // (A parameter's error is at the whole declaration, the one span is given for: tsc's getErrorSpanForNode has no
    // case for it)
    Span at = !my parent && span_valid(span) ? span : my content_span;
    if (strict_empty) {
        u32 reported = initializer ?
            ty_check_non_null_non_void_at(current_checker, ty_expr_type(current_checker, initializer), at, true, errs) : 0;
        if (my tag == BINDING_OBJECT) {
            Ty const* type = ty_binding_type(current_checker, me, (LowerCtx){ .generic = true });
            // (The same diagnostic twice is one)
            ErrorCtx quiet = error_ctx_scratch(errs);
            u32 again = ty_check_non_null_non_void_at(current_checker, type, at, true, &quiet);
            if (again && again != reported) error_ctx_merge(errs, &quiet);
            error_ctx_drop(&quiet);
        }
    }
    // (A catch variable's annotation isn't assigned anything: TS1196 is its only check. Nor is a variable's or
    // parameter's without initializer: its any isn't assignable to `never`)
    if (my has_explicit_type && (my parent || (my source && assigned)) && !(strict_empty && initializer) &&
        !is_js_expando_object_initializer(me, initializer)
    ) {
        // (A parameter pattern's initializer as it is, not padded with the pattern's defaults: tsc's
        // checkVariableLikeDeclaration checks checkExpressionCached(initializer))
        bool padded = !my parent && initializer && my tag != BINDING_IDENTIFIER && binding_is_parameter(me);
        Ty const* assigned_type = padded ? ty_expr_type(current_checker, initializer) : ty_binding_assigned_type(current_checker, me);
        ty_check_assignable_expr(current_checker, assigned_type,
            ty_of_node(&my explicit_type), span_valid(span) ? span : my content_span, initializer, errs);
    }

    if (my default_value) {
        Ty const* default_type = ty_binding_default_type(current_checker, me);
        expr_typecheck(my default_value, errs);
        if (my tag == BINDING_ARRAY && binding_is_parameter(me)) binding_add_padded_implicit_any_errors(me, default_type, errs);
        // An element's default is assigned to the element (tsc's checkVariableLikeDeclaration: the initializer of the
        // symbol's value declaration, or of a pattern, against its widened type). Only a written type on the root can
        // make that fail: else the element's type is the union of its property and the default
        if (my parent) {
            Binding const* root = my parent;
            while (root->parent) root = root->parent;
            if (root->has_explicit_type) {
                Ty const* type = ty_binding_type(current_checker, me, (LowerCtx){ .generic = true });
                ty_check_assignable_expr(current_checker, default_type, type, my content_span, my default_value, errs);
            }
        }
    }

    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            if (my symbol) {
                check_subsequent_declaration(me, errs);
                Ty const* type = ty_binding_type(current_checker, me, (LowerCtx){ .generic = true });
                // (tsc's reportCircularityError)
                if (type == ty_intrinsic(current_checker, TY_CIRCULAR) && my has_explicit_type) {
                    error_ctx_error_d(errs, my content_span, 2502,
                        "'", my content_span, "' is referenced directly or indirectly in its own type annotation.");
                } else if (type == ty_intrinsic(current_checker, TY_CIRCULAR) &&
                    compiler_options()->no_implicit_any && !my has_explicit_type
                ) {
                    error_ctx_error_d(errs, my content_span, 7022,
                        "'", my content_span, "' implicitly has type 'any' because it does not have a type annotation and is referenced directly or indirectly in "
                        "its own initializer.");
                } else if (my parent && compiler_options()->no_implicit_any) {
                    // (tsc's reportErrorsFromWidening of a binding element)
                    Ty const* raw = ty_element_before_widening(current_checker, me);
                    if (!raw) raw = parameter_element_before_widening(me);
                    if (raw && !binding_report_widening_errors_in_type(raw, errs)) {
                        error_ctx_error_d(errs, my content_span, 7031, "Binding element '", my content_span,
                            "' implicitly has an '", type, "' type.");
                    }
                }
            }
            break;

        case BINDING_ARRAY:
            if (compiler_options()->target < SCRIPT_TARGET_ES2015 && compiler_options()->downlevel_iteration && !my ambient) {
                ty_check_emit_helpers(current_checker, my content_span, EMIT_HELPER_READ, errs);
            }
            Ty const* type = ty_binding_type(current_checker, me, (LowerCtx){ .generic = true });
            // (Not widened: `= null` is null, not any, without strictNullChecks. tsc's getTypeForBindingElementParent)
            Expr const* source = my parent ? my default_value : initializer;
            if (!my has_explicit_type && source && !(type->flags & TYF_NULLABLE)) {
                Ty const* assigned = ty_expr_type(current_checker, source);
                if (assigned->flags & TYF_NULLABLE) type = assigned;
            }
            ty_iterated_type(current_checker, ITERATION_DESTRUCTURING, type, empty_pattern ? at : my content_span, errs);
            Ty const* parent = element_parent_type(me, nullptr);
            for_each (i, vec_binding, &my array) {
                if (i->rest && i_index + 1 < vec_binding_size(&my array)) { // (tsc's checkGrammarBindingElement)
                    error_ctx_grammar_error_d(errs, i->content_span, 2462, "A rest element must be last in a destructuring pattern.");
                } else if (i->rest) {
                    binding_report_rest_trailing_comma(i, errs);
                    if (i->default_value) binding_report_rest_initializer(i, errs);
                }
                // Past the end of a tuple (TS2493), unless there's a default
                if (parent && i->tag != BINDING_NOOP && !i->rest && !i->default_value) {
                    Value index = value_number((double)i_index);
                    ty_check_property_access(current_checker, parent, ty_literal(current_checker, &index), span_null,
                        i->content_span, false, false, errs);
                }
                binding_typecheck(i, nullptr, errs);
            }
            break;

        case BINDING_OBJECT: {
            Ty const* parent = element_parent_type(me, my parent ? my default_value : initializer);
            // (tsc's isThisInitializedDeclaration: the pattern's own declaration is initialized with `this`)
            bool from_this = !my parent && initializer && initializer->tag == EXPR_IDENTIFIER &&
                initializer->identifier.keyword == KEYWORD_IDENTIFIER_THIS && !expr_is_perenthisized(initializer);
            for_each (i, vec_object_binding_item, &my object) {
                if (i->target.rest && i_index + 1 == vec_object_binding_item_size(&my object)) {
                    binding_report_rest_trailing_comma(&i->target, errs);
                }
                if (i->target.rest && i_index + 1 < vec_object_binding_item_size(&my object)) {
                    error_ctx_grammar_error_d(errs, i->target.content_span, 2462, "A rest element must be last in a destructuring pattern.");
                                    } else if (i->target.rest_property_name) {
                    error_ctx_grammar_error_d(errs, i->target.content_span, 2566, "A rest element cannot have a property name.");
                } else if (i->target.rest && i->target.default_value) {
                    binding_report_rest_initializer(&i->target, errs);
                } else if (i->target.rest && !ty_object_rest_source_valid(current_checker,
                    ty_binding_type(current_checker, me, (LowerCtx){ .generic = true }))
                ) {
                    // (tsc's getTypeForBindingElement: isValidSpreadType of the parent)
                    error_ctx_error_d(errs, i->target.content_span, 2700, "Rest types may only be created from object types.");
                }
                if (parent && !i->computed_key && !i->target.rest && !(i->target.default_value &&
                    parent->kind == TY_ANONYMOUS && parent->anonymous.object_literal)
                ) {
                    ty_check_property_access(current_checker, parent, ty_literal(current_checker, &i->source), span_null,
                        span_valid(i->source_span) ? i->source_span : i->target.content_span, false, true, errs);
                }
                if (i->computed_key) {
                    expr_typecheck(i->computed_key, errs);
                    // (tsc's getTypeForBindingElement: getIndexedAccessType with the name as the access node)
                    // (With a default, an object literal may lack it: AccessFlags.AllowMissing)
                    if (parent && !i->target.rest && !(i->target.default_value && parent->kind == TY_ANONYMOUS &&
                        parent->anonymous.object_literal)
                    ) {
                        Span key_span = expr_outer_span(i->computed_key);
                        ty_check_destructuring_indexed_access(current_checker, parent, expr_get_type(i->computed_key),
                            key_span, errs);
                    }
                    if (parent && !i->target.rest) {
                        ty_check_destructured_member(current_checker, parent, ty_regular(current_checker,
                            expr_get_type(i->computed_key)), span_valid(i->source_span) ? i->source_span : expr_outer_span(i->computed_key),
                            from_this, my access_site, errs);
                    }
                } else if (!i->target.rest && (i->source.tag == VALUE_STRING || i->source.tag == VALUE_NUMBER)) {
                    Ty const* type = parent ? parent : unused_enabled() ?
                        ty_binding_type(current_checker, me, (LowerCtx){ .generic = true }) : nullptr;
                    if (type) {
                        ty_check_destructured_member(current_checker, type, ty_literal(current_checker, &i->source),
                            span_valid(i->source_span) ? i->source_span : i->target.content_span, from_this, my access_site,
                            errs);
                    }
                }
                if (i->target.rest && compiler_options()->target < SCRIPT_TARGET_ES2018 && !my ambient) {
                    ty_check_emit_helpers(current_checker, i->target.content_span, EMIT_HELPER_REST, errs);
                }
                binding_typecheck(&i->target, nullptr, errs);
            }
            break;
        }
    }
}

void object_binding_item_drop(ObjectBindingItem* me) {
    if (my computed_key) {
        expr_drop(my computed_key);
        free(my computed_key);
    }
    value_drop(&my source);
    binding_drop(&my target);
}

const Expr initialized_binding_no_value = { .tag = EXPR_IMPLICIT_ANY, .lazy_type.tag = LAZY_TYPE_EVALUATED };

const InitializedBindingRare initialized_binding_no_rare = {0};

InitializedBindingRare* initialized_binding_rare_mut(InitializedBinding* me) {
    if (!my rare) my rare = c_new(InitializedBindingRare, {0});
    return my rare;
}

void initialized_binding_drop(InitializedBinding* me) {
    if (my rare) {
        for (u32 i = 0; i < my rare->require_port_count; i++) port_item_drop(&my rare->require_ports[i]);
        free(my rare->require_ports);
        free(my rare);
    }
    binding_drop(&my binding);
    if (my value) {
        expr_drop(my value);
        free(my value);
    }
    lazy_type_drop(&my lazy_type);
}

void initialized_binding_add_names(InitializedBinding* me, Scope* scope, BindingAddNamesProps props) {
    lazy_type_bind(&my lazy_type, LAZY_TYPE_INITIALIZED_BINDING);
    my abstract_literals = props.abstract_literals;
    if (!initialized_binding_rare(me)->require_port_count) {
        binding_add_names(&my binding, &my lazy_type, scope, props);
        return;
    }
    // The names are aliases (see require_ports), exported like `export import x = require()`. Declared in the function
    // or file around a block, like a `var` (tsc's declareSymbolAndAddToSymbolTable puts aliases in the container's
    // locals: two `const X = require()` in different blocks are TS2300, microsoft/TypeScript#41697)
    while (scope->parent && !scope_is_var_container(scope)) scope = scope->parent;
    my binding.source = &my lazy_type;
    for (u32 i = 0; i < initialized_binding_rare(me)->require_port_count; i++) initialized_binding_rare(me)->require_ports[i].export_import = props.exported;
    if (my binding.tag == BINDING_IDENTIFIER) {
        scope_add_port(scope, &initialized_binding_rare(me)->require_ports[0]);
        my binding.symbol = initialized_binding_rare(me)->require_ports[0].symbol;
        return;
    }
    for_each (i, vec_object_binding_item, &my binding.object) {
        i->target.parent = &my binding;
        i->target.parent_item = i;
        PortItem* port = &initialized_binding_rare(me)->require_ports[i_index];
        scope_add_port(scope, port);
        i->target.symbol = port->symbol;
    }
}

// A `const` declaration of a variable can have a unique symbol type: its annotation and a `Symbol()` initializer say so
void unique_symbol_decl_mark(UniqueSymbolDecl decl, TypeNode* annotation, Expr* initializer) {
    if (annotation && annotation->tag == TYPE_UNIQUE_SYMBOL) annotation->unique_symbol = decl;
    if (!initializer || initializer->tag != EXPR_CALL || initializer->call->uses_new) return;
    // (Only `Symbol()` or `Symbol.for()` makes one, see is_symbol_call())
    CallExpr* call = initializer->call;
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_PROPERTY && span_equals(callee->property->prop.inner_span, "for")) callee = &callee->property->expr;
    if (callee->tag != EXPR_IDENTIFIER || !csview_equals(callee->inner_span.sv, "Symbol")) return;
    if (!call->unique_symbol) call->unique_symbol = malloc(sizeof(UniqueSymbolDecl));
    *call->unique_symbol = decl;
}

void initialized_binding_lookup_names(InitializedBinding* me, Scope* names, ErrorCtx* errs) {
    if (my binding.tag == BINDING_IDENTIFIER && my binding.symbol && my binding.symbol->is_const) {
        unique_symbol_decl_mark(
            (UniqueSymbolDecl){ .key = my binding.symbol, .owner = my binding.symbol },
            &my binding.explicit_type, initialized_binding_value(me)
        );
    }
    if (initialized_binding_rare(me)->require_port_count) module_ref_report(initialized_binding_rare(me)->require_ports[0].module_ref, names, errs);
    InitializedBinding const* outer = names->flow->initializing;
    names->flow->initializing = me;
    binding_lookup_names(&my binding, names, errs);
    names->flow->contextual_depth += my binding.has_explicit_type;
    if (my binding.tag == BINDING_IDENTIFIER && initialized_binding_value(me)->tag == EXPR_FUNCTION && !function_impl_is_arrow(initialized_binding_value(me)->function) &&
        names->file && names->file->is_js
    ) {
        function_impl_rare_mut(initialized_binding_value(me)->function)->class_symbol = my binding.symbol;
        scope_mark_jsdoc_class(names->binder, my binding.symbol, initialized_binding_value(me)->function);
    }
    // (tsc's getNameOfSymbolAsWritten: an anonymous function is named by the variable it initializes)
    if (my binding.tag == BINDING_IDENTIFIER && initialized_binding_value(me)->tag == EXPR_FUNCTION) function_impl_set_variable_name(initialized_binding_value(me)->function, my binding.content_span);
    if (my value) expr_lookup_names(my value, names, errs);
    names->flow->contextual_depth -= my binding.has_explicit_type;
    names->flow->initializing = outer;
    if (initialized_binding_value(me)->tag != EXPR_IMPLICIT_ANY) binding_flow_assign(&my binding, names);
}

void initialized_binding_lookup_names_parameter(InitializedBinding* me, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    Binding const* saved = flow->parameter_initializing;
    Scope const* saved_scope = flow->parameter_function_scope;
    flow->parameter_initializing = &my binding;
    flow->parameter_function_scope = names->parent; // `names` is the parameter list's own scope
    binding_lookup_names(&my binding, names, errs);
    flow->contextual_depth += my binding.has_explicit_type;
    if (my value) expr_lookup_names_initializer(my value, names, errs);
    flow->contextual_depth -= my binding.has_explicit_type;
    flow->parameter_initializing = saved;
    flow->parameter_function_scope = saved_scope;
}

// The initializer, checked with the type annotation (if any) as its contextual type
static Ty const* initialized_binding_check_value(InitializedBinding* me) {
    Checker* checker = current_checker;
    Ty const* contextual = nullptr;
    if (my binding.has_explicit_type) {
        contextual = ty_from_node(checker, &my binding.explicit_type, (LowerCtx){ .generic = true });
    } else if (binding_is_parameter(&my binding) && ty_contextual_parameter_type(checker, me)) {
        // A parameter's contextual type (an immediately invoked function's argument: tsc's
        // getContextuallyTypedParameterType), before what its pattern implies
        contextual = ty_contextual_parameter_type(checker, me);
    } else if ((my binding.tag == BINDING_OBJECT && vec_object_binding_item_size(&my binding.object)) ||
        (my binding.tag == BINDING_ARRAY && vec_binding_size(&my binding.array))
    ) {
        // Else what a pattern implies (tsc's getContextualTypeForInitializerExpression): `[1, "a"]` is a tuple there
        contextual = ty_binding_pattern_type(checker, &my binding, true);
    }
    return ty_check_expr(checker, initialized_binding_value(me), contextual);
}

// `let x;`, `let x = null` and `let x = []` under noImplicitAny (or in a JavaScript file): the type comes from what is
// assigned (and pushed), as control flow sees it. tsc's autoType and autoArrayType in getWidenedTypeForVariableLike
// Declaration: TY_AUTO, ty_auto_array(), else null
static Ty const* auto_type(InitializedBinding const* me) {
    SourceFile const* file = my binding.content_span.source;
    if (my binding.has_explicit_type || my binding.tag != BINDING_IDENTIFIER ||
        !(compiler_options()->no_implicit_any || (file && file->is_js))
    ) {
        return nullptr;
    }
    Symbol const* symbol = my binding.symbol;
    if (!symbol || !(symbol->flags & SYMBOL_VARIABLE) || symbol->is_parameter || symbol->is_ambient || symbol->exported ||
        symbol->parent
    ) {
        return nullptr;
    }
    bool auto_any = initialized_binding_value(me)->tag == EXPR_IMPLICIT_ANY ? !initialized_binding_getter(me) :
        initialized_binding_value(me)->tag == EXPR_LITERAL && (initialized_binding_value(me)->literal.tag == VALUE_NULL || initialized_binding_value(me)->literal.tag == VALUE_UNDEFINED);
    if (auto_any) return symbol->is_const ? nullptr : ty_intrinsic(current_checker, TY_AUTO);
    if (initialized_binding_value(me)->tag == EXPR_ARRAY && !vec_expr_size(&initialized_binding_value(me)->array)) return ty_auto_array(current_checker);
    return nullptr;
}

FunctionImpl const* initialized_binding_getter(InitializedBinding const* me) {
    if (!initialized_binding_rare(me)->getter) return nullptr;
    Expr const* key = initialized_binding_rare(me)->getter_key;
    if (!key || key->tag == EXPR_LITERAL) return initialized_binding_rare(me)->getter;
    Ty const* type = ty_expr_type(current_checker, (Expr*)key);
    return type->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL) ? initialized_binding_rare(me)->getter : nullptr;
}

Ty const* initialized_binding_evaluate_assigned_type(InitializedBinding* me) {
    Checker* checker = current_checker;
    FunctionImpl const* getter = initialized_binding_value(me)->tag == EXPR_IMPLICIT_ANY && !my binding.has_explicit_type ?
        initialized_binding_getter(me) : nullptr;
    if (getter) return ty_function_return_type(checker, getter);
    if (!my binding.has_explicit_type) {
        // A parameter of a function expression gets its type from the contextual signature, initializer or not
        if (initialized_binding_rare(me)->jsdoc_typed_by) ty_function_apply_jsdoc_type(checker, initialized_binding_rare(me)->jsdoc_typed_by, me);
        Ty const* contextual = ty_contextual_parameter_type(checker, me);
        if (contextual) return contextual;
        if (my is_rest_parameter && initialized_binding_value(me)->tag == EXPR_IMPLICIT_ANY && my binding.tag == BINDING_IDENTIFIER) {
            return ty_array(checker, ty_intrinsic(checker, TY_ANY), false);
        }
        // An unannotated pattern without a default takes what the pattern implies (tsc's getWidenedTypeForVariableLikeDeclaration),
        // a rest parameter's too (`...{ a, b }`, `...[x, y]`)
        if (initialized_binding_value(me)->tag == EXPR_IMPLICIT_ANY && (my binding.tag == BINDING_OBJECT || my binding.tag == BINDING_ARRAY)) {
            return ty_binding_pattern_type(checker, &my binding, false);
        }
    }
    Ty const* auto_typed = auto_type(me);
    if (auto_typed) return auto_typed;
    // A call of a single non-generic signature has its type without its arguments being checked (tsc's
    // checkDeclarationInitializer): `const r = f(x)` doesn't depend on x, which may depend on r through a loop
    Ty const* ret = ty_quick_expr_type(checker, initialized_binding_value(me), false);
    if (!ret) ret = initialized_binding_check_value(me);
    if (my binding.tag == BINDING_OBJECT || my binding.tag == BINDING_ARRAY) ret = ty_pad_initializer_type(checker, ret, &my binding);
    Symbol const* symbol = my binding.tag == BINDING_IDENTIFIER ? my binding.symbol : nullptr;
    // A pattern's elements are widened one by one (ty_binding_assigned_type()), the object they come from isn't (tsc's
    // getTypeForBindingElementParent): `{ a, b } = options || {}` reads the members of `{ a: A; b: B } | {}`, where the
    // widened `{}` would absorb the other member
    // (A parameter's pattern is its declared type too, which is widened)
    bool pattern_source = my binding.tag != BINDING_IDENTIFIER && !binding_is_parameter(&my binding);
    if (!my binding.has_explicit_type && !pattern_source) {
        ret = ty_widened_for_declaration(checker, ret, my binding.tag == BINDING_IDENTIFIER && my abstract_literals);
        // A unique symbol made for another declaration is only a symbol here (tsc's widenTypeForVariableLikeDeclaration)
        if ((ret->flags & TYF_UNIQUE_ES_SYMBOL) && ret->literal.unique->key != symbol) {
            ret = ty_intrinsic(checker, TY_ES_SYMBOL);
        }
    }
    return ret;
}

// tsc's reportWideningErrorsInType: TS7018 on the object literal properties whose null/undefined widen to any. A union
// with an empty object type in it counts as reported (tsc: nothing is said)
static bool report_widening_errors_in_type(Checker* checker, Ty const* ty, ErrorCtx* errs, u32 depth) {
    if (depth > 8 || !(ty->xflags & TYX_CONTAINS_WIDENING)) return false;
    bool reported = false;
    if (ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (ty_is_empty_object_type(checker, ty->composite.types.data[i])) return true;
        }
        for (u32 i = 0; i < ty->composite.types.len; i++) reported |= report_widening_errors_in_type(checker, ty->composite.types.data[i], errs, depth + 1);
    } else if (ty->kind == TY_REFERENCE && (ty_is_array(checker, ty) || ty_tuple_shape(ty))) {
        TyList args = ty_reference_args(checker, ty);
        for (u32 i = 0; i < args.len; i++) reported |= report_widening_errors_in_type(checker, args.data[i], errs, depth + 1);
    } else if (ty->kind == TY_ANONYMOUS && ty->anonymous.object_literal) {
        Members const* members = ty_members(checker, ty);
        for (u32 i = 0; i < members->prop_count; i++) {
            Prop const* prop = members->props[i];
            if (prop->decl->function) continue;
            Ty const* t = prop_type(checker, prop);
            if (!(t->xflags & TYX_CONTAINS_WIDENING)) continue;
            bool inner = report_widening_errors_in_type(checker, t, errs, depth + 1);
            // (At the property's declaration, `a: null` and not just its name: tsc's valueDeclaration, a spread one's too)
            if (!inner && prop->decl->decl_span_count && prop->decl->decl_spans[0].source) {
                cstr name = ty_prop_name_cstr(checker, prop->name);
                cstr type = ty_to_cstr(checker, ty_widened_nullable(checker, t));
                error_ctx_error_d(errs, prop->decl->decl_spans[0], 7018, "Object literal's property '", name, "' implicitly has an '", type, "' type.");
                c_drop(cstr, &name, &type);
                inner = true;
            }
            reported |= inner;
        }
    }
    return reported;
}

bool binding_report_widening_errors_in_type(Ty const* ty, ErrorCtx* errs) {
    return report_widening_errors_in_type(current_checker, ty, errs, 0);
}

// `parameter`: the whole parameter when `me` is one (tsc reports at the declaration, a variable's at its name)
static void report_errors_from_widening(InitializedBinding* me, Span parameter, ErrorCtx* errs) {
    Checker* checker = current_checker;
    if (!compiler_options()->no_implicit_any || compiler_options()->strict_null_checks) return;
    if (my binding.has_explicit_type || initialized_binding_value(me)->tag == EXPR_IMPLICIT_ANY || my binding.tag != BINDING_IDENTIFIER) return;
    if (auto_type(me) || my is_rest_parameter) return;
    Symbol const* symbol = my binding.symbol;
    if (!symbol) return;
    if (symbol->is_parameter && ty_contextual_parameter_type(checker, me)) return;
    Ty const* ty = ty_expr_type(checker, initialized_binding_value(me));
    if (!(ty->xflags & TYX_CONTAINS_WIDENING)) return;
    if (report_widening_errors_in_type(checker, ty, errs, 0)) return;
    cstr type = ty_to_cstr(checker, ty_widened_nullable(checker, ty));
    error_ctx_error_d(errs, span_valid(parameter) ? parameter : my binding.content_span, symbol->is_parameter ? 7006 : 7005, symbol->is_parameter ? "Parameter '" : "Variable '",
        my binding.content_span, "' implicitly has an '", type, "' type.");
    cstr_drop(&type);
}

void initialized_binding_typecheck(InitializedBinding* me, ErrorCtx* errs) {
    // `const x = require("m")`, `const x = require("m").a`: an alias, which is all tsc checks
    // (checkVariableLikeDeclaration: checkAliasSymbol and return)
    if (initialized_binding_rare(me)->require_port_count) return;
    // tsc's checkSourceElement of the VariableDeclaration: what goes wrong deep inside is reported at its name, and
    // checkVariableLikeDeclaration asks for the variable's type first, which takes a call's return type without
    // checking the call (getQuickTypeOfExpression): an instantiation limit it runs into is the declaration's (TS2589)
    Checker* checker = current_checker;
    CurrentNode saved = ty_enter_node(checker, my binding.content_span);
    // (The annotation is walked first, which makes the types of the literals in it: tsc's checkVariableLikeDeclaration)
    if (my binding.has_explicit_type) type_typecheck(&my binding.explicit_type, errs);
    if (my binding.tag == BINDING_IDENTIFIER && my binding.symbol && initialized_binding_value(me)->tag == EXPR_CALL) {
        ty_type_of_symbol(checker, my binding.symbol);
    }
    initialized_binding_check_value(me);
    expr_typecheck(initialized_binding_value(me), errs);
    Expr const* initializer = initialized_binding_value(me)->tag == EXPR_IMPLICIT_ANY ? nullptr : initialized_binding_value(me);
    binding_typecheck_at(&my binding, initializer, span_null, initializer, true, errs);
    report_errors_from_widening(me, span_null, errs);
    ty_leave_node(checker, saved);
}

void initialized_binding_typecheck_parameter(InitializedBinding* me, Span span, ErrorCtx* errs) {
    if (my binding.has_explicit_type) type_typecheck(&my binding.explicit_type, errs);
    initialized_binding_check_value(me);
    expr_typecheck(initialized_binding_value(me), errs);
    Expr const* initializer = initialized_binding_value(me)->tag == EXPR_IMPLICIT_ANY ? nullptr : initialized_binding_value(me);
    binding_typecheck_at(&my binding, initializer, span, initializer, true, errs);
    report_errors_from_widening(me, span, errs);
    if (!initializer || my binding.has_explicit_type) return;
    // An unannotated parameter typed by its contextual signature: the initializer is assigned to that type, unless it
    // was the initializer's type instead (tsc's assignContextualParameterTypes: when the contextual type is assignable
    // to the widened initializer but not the other way around)
    Checker* checker = current_checker;
    if (!ty_contextual_parameter_type(checker, me)) return;
    Ty const* type = ty_binding_type(checker, &my binding, (LowerCtx){ .generic = true });
    Ty const* value = ty_expr_type(checker, initializer);
    if (ty_is_related(checker, value, type, TY_RELATION_ASSIGNABLE)) return;
    if (ty_is_related(checker, type, ty_widened_for_declaration(checker, value, false), TY_RELATION_ASSIGNABLE)) return;
    ty_check_assignable_expr(checker, value, type, span, initializer, errs);
}
