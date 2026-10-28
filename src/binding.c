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
    panic_if(span_source(span) != span_source(my content_span), "a binding's span in another file than its name");
    *start = (u32)(span.buf - source_file_start(span_source(span)));
    *length = (u32)span.size;
}

Span binding_explicit_type_span(Binding const* me) {
    if (!my has_explicit_type_span) return span_null;
    char const* start = source_file_start(span_source(my content_span)) + my explicit_type_start;
    return span_in_sv(my content_span, csview_from_n(start, my explicit_type_length));
}

Span binding_element_span(Binding const* me) {
    if (!my has_element_span) return span_null;
    char const* start = source_file_start(span_source(my content_span)) + my element_start;
    return span_in_sv(my content_span, csview_from_n(start, my element_length));
}

void binding_set_explicit_type_span(Binding* me, Span span) {
    my has_explicit_type_span = span_valid(span);
    if (my has_explicit_type_span) span_to_offsets(me, span, &my explicit_type_start, &my explicit_type_length);
}

void binding_set_element_span(Binding* me, Span span) {
    my has_element_span = span_valid(span);
    if (my has_element_span) span_to_offsets(me, span, &my element_start, &my element_length);
}

const TypeNode type_node_none = {0};

void binding_set_type(Binding* me, TypeNode type) {
    if (my explicit_type) {
        type_drop(my explicit_type);
        ast_delete(my explicit_type);
        my explicit_type = nullptr;
    }
    my explicit_type = ast_new(TypeNode, type);
}

TypeNode binding_take_type(Binding* me) {
    if (!my explicit_type) return (TypeNode){0};
    TypeNode ret = *my explicit_type;
    ast_delete(my explicit_type);
    my explicit_type = nullptr;
    return ret;
}

void binding_drop(Binding* me) {
    switch (my tag) {
        case BINDING_NOOP: break;
        case BINDING_IDENTIFIER: break;
        case BINDING_ARRAY: vec_binding_drop(&my array); break;
        case BINDING_OBJECT: vec_object_binding_item_drop(&my object); break;
    }
    ast_delete(my access_site);
    if (my explicit_type) type_drop(my explicit_type);
    ast_delete(my explicit_type);
    if (my default_value) {
        expr_drop(my default_value);
        ast_delete(my default_value);
    }
}

void binding_add_names(Binding* me, LazyType* source, Scope* scope, BindingAddNamesProps props) {
    my source = source;
    my ambient = props.is_ambient || scope->ambient;

    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            my is_parameter = props.is_function_arg;
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
                .declare_keyword = props.declare_keyword,
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
    csview name = span_sv(my content_span);
    for (Scope const* scope = names; scope && scope->kind == SCOPE_BLOCK; scope = scope->parent) {
        Symbol const* found = symbol_table_get(scope_locals(scope), name);
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
    Binding const* outer_root = scope_flow(names) ? scope_flow(names)->binding_root : nullptr;
    if (scope_flow(names) && !my parent) scope_flow(names)->binding_root = me;
    // The type, then the default, then the pattern: a default is evaluated before the elements it is destructured
    // into (tsc's bindParameterFlow / bindBindingElementFlow)
    if (my has_explicit_type && my explicit_type) {
        type_lookup_names(my explicit_type, names, errs);
    }
    if (my default_value) {
        // In a parameter's pattern the default is the element's own (tsc's associatedDeclarationForContainingInitializer)
        Binding const* saved = scope_flow(names) ? scope_flow(names)->parameter_initializing : nullptr;
        if (saved) scope_flow(names)->parameter_initializing = me;
        expr_lookup_names_initializer(my default_value, names, errs);
        if (saved) scope_flow(names)->parameter_initializing = saved;
    }
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            // The flow container the variable belongs to: elsewhere it is assumed to be initialized
            // (Only the first declaring file's walk writes it: others may be running on other threads, plan 45)
            if (my symbol && scope_flow(names) && !symbol_first_declared_elsewhere(my symbol, names->file) && !my symbol->flow_start &&
                walk_writes_flow_start(my symbol)
            ) {
                my symbol->flow_start = scope_flow(names)->container_start;
            }
            binding_check_var_not_shadowed(me, names, errs);
            break;

        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) {
                binding_lookup_names(i, names, errs);
            }
            break;

        case BINDING_OBJECT:
            ast_delete(my access_site);
            my access_site = scope_access_site(names);
            for_each (i, vec_object_binding_item, &my object) {
                if (i->computed_key) {
                    expr_lookup_names(i->computed_key, names, errs);
                }
                binding_lookup_names(&i->target, names, errs);
            }
            break;
    }

    if (scope_flow(names) && !my parent) scope_flow(names)->binding_root = outer_root;
}

// The default of a parameter or of an element of a pattern is only evaluated sometimes (tsc: bindInitializer())
void expr_lookup_names_initializer(Expr* me, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = scope_flow(names);
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
            flow_assign(scope_flow(names), (FlowAssign){ .kind = FLOW_ASSIGN_DECLARATION, .binding = me });
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
                toplevel ? "Parameter" : "Binding element", " '", span_name_text(my content_span), "' implicitly has an 'any' type."
            );
            break;

        case BINDING_ARRAY:
            // (A lone rest element is `Iterable<any>` without looking at it: getTypeFromArrayBindingPattern)
            if (vec_binding_size(&my array) == 1 && vec_binding_at(&my array, 0)->rest) break;
            for_each (i, vec_binding, &my array) {
                binding_add_implicit_any_type_errors(i, span_null, bodiless, errs);
            }
            break;

        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                // (An object rest element is an index signature to any: getTypeFromObjectBindingPattern)
                if (i->target.rest) continue;
                // (A computed key whose type isn't usable as a property name isn't in the implied type either: the
                // element is the implied type indexed by the key, any with a rest element, else TS2537 and the error
                // type, not implicitly any: tsc's getTypeFromObjectBindingPattern, isTypeUsableAsPropertyName)
                if (i->computed_key && !ty_literal_name(current_checker, ty_binding_computed_key_type(current_checker, i))) continue;
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
    if (!compiler_options()->no_implicit_any) return;
    if (my tag == BINDING_OBJECT) {
        // (tsgo's padObjectLiteralType: an element without a default the object literal lacks is reported by
        // getTypeFromBindingElement)
        if (initializer->kind != TY_ANONYMOUS || !initializer->anonymous.object_literal) return;
        for_each (item, vec_object_binding_item, &my object) {
            Binding const* element = &item->target;
            if (element->default_value || element->rest || item->computed_key || item->private_name ||
                element->tag != BINDING_IDENTIFIER
            ) {
                continue;
            }
            bool is_number = false;
            Atom const* name = ty_property_name_of_key(current_checker, ty_literal(current_checker, &item->source), &is_number);
            if (!name || ty_get_prop(current_checker, initializer, name)) continue;
            error_ctx_error_d(errs, element->content_span, 7031, "Binding element '", span_name_text(element->content_span),
                "' implicitly has an 'any' type.");
        }
        return;
    }
    if (my tag != BINDING_ARRAY) return;
    TupleShape const* shape = ty_tuple_shape(initializer);
    if (!shape) return;
    for (u32 i = 0; i < shape->arity; i++) {
        if (shape->flags[i] & TUPLE_ELEMENT_REST) return;
    }
    u32 len = (u32)vec_binding_size(&my array);
    for (u32 i = shape->arity; i < len; i++) {
        Binding const* element = vec_binding_at(&my array, i);
        if (element->default_value || (i == len - 1 && element->rest)) continue;
        // (A hole too, an OmittedExpression to tsc, whose name prints as `(Missing)`)
        if (element->tag == BINDING_NOOP) {
            if (span_source(element->content_span)) {
                error_ctx_error_d(errs, element->content_span, 7031, "Binding element '(Missing)' implicitly has an 'any' type.");
            }
            continue;
        }
        // (tsc's reportImplicitAny of a binding element: the name for an identifier, else the whole pattern)
        error_ctx_error_d(errs, element->content_span, 7031, "Binding element '", span_name_text(element->content_span),
            "' implicitly has an 'any' type.");
    }
}

// tsc's checkVariableLikeDeclaration for a declaration that isn't the symbol's first (`var x: A; var x: B`): the same
// type (TS2403)
static void check_subsequent_declaration(Binding const* me, ErrorCtx* errs) {
    if (!my content_span.size) return; // (a missing name: recovery's, nothing is said about it)
    Symbol const* symbol = my symbol;
    SymbolDecl const* first = nullptr;
    // (The namespace an expando makes of a name isn't its value declaration: tsc's setValueDeclaration)
    for (u32 i = 0; i < symbol->decl_count && !first; i++) {
        if (symbol->decls[i].flags & SYMBOL_VALUE && symbol->decls[i].kind != SYMBOL_DECL_EXPANDO) first = &symbol->decls[i];
    }
    if (!first || first->node == me) return;
    Checker* checker = current_checker;
    // Not for a symbol a top-level assignment made a namespace (tsc: SymbolFlags.Assignment)
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_EXPANDO || !span_valid(symbol->decls[i].full)) continue;
        // The initializer is still checked against the declaration's type, which for a JavaScript `const A = {}`
        // container is an object of the merged symbol's (tsc's getJSContainerObjectType): a class or function
        // declared first makes it that one's type (`typeof A`)
        Expr const* value = !my parent && my source && my source->tag == LAZY_TYPE_INITIALIZED_BINDING && !my has_explicit_type ?
            initialized_binding_value(c_container_of(me, InitializedBinding, binding)) : nullptr;
        if (value && value->tag == EXPR_OBJECT && !vec_object_expr_item_size(&value->object) && span_source(my content_span) &&
            span_source(my content_span)->is_js && first->flags & (SYMBOL_CLASS | SYMBOL_FUNCTION)
        ) {
            ty_check_assignable_expr(checker, ty_expr_type(checker, value), ty_type_of_symbol(checker, symbol), my content_span,
                value, errs);
        }
        return;
    }
    Ty const* type = ty_type_of_symbol(checker, symbol);
    Ty const* declared = ty_binding_type(checker, me, (LowerCtx){ .generic = true });
    Ty const* circular = ty_intrinsic(checker, TY_CIRCULAR);
    // (A first declaration whose annotation references itself (TS2502) is the error type: tsc's reportCircularityError)
    Binding const* first_annotated = first->kind == SYMBOL_DECL_VARIABLE ? first->node : nullptr;
    if (first_annotated && first_annotated->has_explicit_type &&
        ty_binding_type(checker, first_annotated, (LowerCtx){ .generic = true }) == circular
    ) {
        return;
    }
    // (Any error type, an unresolved name's too: tsc's isErrorType; the identity relation has no shortcut for it)
    if ((type->flags & TYF_ERROR) || (declared->flags & TYF_ERROR) || declared == circular ||
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
Ty const* binding_element_parent_type(Binding const* me, Expr const* initializer) {
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

// What an object pattern's rest element is taken from (tsc's getTypeForBindingElement in CheckMode.RestBindingElement):
// a root pattern without a written type types its initializer again without the pattern as contextual type, so a
// reference to a `T` the pattern made its constraint stays `T` (not the never a discriminant narrowed that to)
static Ty const* binding_rest_source(Binding const* me, Expr const* initializer) {
    Ty const* type = ty_binding_type(current_checker, me, (LowerCtx){ .generic = true });
    if (my parent || my has_explicit_type || !initializer) return type;
    Ty const* rest = ty_rest_binding_initializer_type(current_checker, initializer);
    return rest ? rest : type;
}

// tsc's checkGrammarBindingElement: a rest element's initializer, at its `=`
static void binding_report_rest_initializer(Binding const* element, ErrorCtx* errs) {
    Span value = expr_outer_span(element->default_value);
    if (!span_valid(value)) return;
    char const* at = span_start_ptr(value);
    char const* start = span_start_ptr(element->content_span);
    while (at > start && at[-1] != '=') at--;
    if (at <= start) return;
    error_ctx_grammar_error_d(errs, span_make_in(value, at - 1, at), 1186, "A rest element cannot have an initializer.");
}

// tsc's checkGrammarBindingElement: a trailing comma after the rest element (checkGrammarForDisallowedTrailingComma)
static void binding_report_rest_trailing_comma(Binding const* element, ErrorCtx* errs) {
    Span extent = element->content_span;
    if (element->default_value) {
        Span value = expr_outer_span(element->default_value);
        if (span_valid(value) && span_valid(extent) && span_end_ptr(value) > span_end_ptr(extent)) {
            extent = span_make_in(extent, span_start_ptr(extent), span_end_ptr(value));
        }
    }
    Span comma = span_comma_after(extent);
    if (span_valid(comma)) {
        error_ctx_grammar_error_d(errs, comma, 1013, "A rest parameter or binding pattern may not have a trailing comma.");
    }
}

static void binding_typecheck_at(Binding* me, Expr const* initializer, Span span, bool assigned, bool type_checked, bool bodiless,
    ErrorCtx* errs);

void binding_typecheck(Binding* me, Expr const* initializer, ErrorCtx* errs) {
    binding_typecheck_at(me, initializer, span_null, true, false, false, errs);
}

void binding_typecheck_bodiless(Binding* me, ErrorCtx* errs) {
    binding_typecheck_at(me, nullptr, span_null, true, false, true, errs);
}

// An unannotated setter parameter typed by its getter: its type is the accessor's (tsc's getTypeOfParameter through
// getTypeOfAccessors), whose circularity the accessor reports (TS7023 on the getter), not the parameter (no TS7022)
static bool setter_parameter_of_getter(Binding const* me) {
    if (my parent || !my source || my source->tag != LAZY_TYPE_INITIALIZED_BINDING) return false;
    InitializedBinding const* initialized = c_container_of(my source, InitializedBinding, lazy_type);
    return initialized_binding_getter(initialized) != nullptr;
}

// `span`: where the initializer's check reports (a parameter's is the whole parameter, tsc's getErrorSpanForNode).
// `assigned`: whether the root is assigned anything (not a variable or parameter without initializer)
// `type_checked`: the annotation was walked already
// `bodiless`: a parameter of a signature without a body, whose elements' initializers are only TS2371
static void binding_typecheck_at(Binding* me, Expr const* initializer, Span span, bool assigned, bool type_checked, bool bodiless,
    ErrorCtx* errs) {
    if (my has_explicit_type && !type_checked) type_typecheck(binding_type(me), errs);
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
    if (my has_explicit_type && (my parent || (my source && assigned)) && !(strict_empty && initializer)) {
        // (A parameter pattern's initializer as it is, not padded with the pattern's defaults: tsc's
        // checkVariableLikeDeclaration checks checkExpressionCached(initializer))
        bool padded = !my parent && initializer && my tag != BINDING_IDENTIFIER && binding_is_parameter(me);
        // (Nor its quick type, ty_quick_expr_type(): the only one that differs is a `new` the checker rejects, an
        // abstract class or an inaccessible constructor, which is the instance type quickly but the error type checked)
        bool rejected_new = !my parent && initializer && initializer->tag == EXPR_CALL && initializer->call->uses_new;
        Ty const* assigned_type = padded || rejected_new ? ty_expr_type(current_checker, initializer) :
            ty_binding_assigned_type(current_checker, me);
        // (`x?: T = v`, TS1015: against the parameter's type, which has undefined, tsc's addOptionality)
        Ty const* target = ty_of_node(binding_type(me));
        if (my optional_parameter && compiler_options()->strict_null_checks && !(target->flags & TYF_ANY)) {
            Ty const* types[2] = { target, ty_intrinsic(current_checker, TY_UNDEFINED) };
            target = ty_union(current_checker, types, 2, nullptr, (TyList){0});
        }
        ty_check_assignable_expr(current_checker, assigned_type, target, span_valid(span) ? span : my content_span,
            initializer, errs);
    }

    // (tsc's checkVariableLikeDeclaration: an element's initializer in a signature without a body is TS2371, after its
    // own elements were checked, and then it returns)
    if (my default_value && my parent && bodiless) {
        if (my tag == BINDING_ARRAY) {
            for_each (i, vec_binding, &my array) binding_typecheck_at(i, nullptr, span_null, true, false, true, errs);
        } else if (my tag == BINDING_OBJECT) {
            for_each (i, vec_object_binding_item, &my object) binding_typecheck_at(&i->target, nullptr, span_null, true, false, true, errs);
        }
        return;
    }
    if (my default_value) {
        Ty const* default_type = ty_binding_default_type(current_checker, me);
        expr_typecheck(my default_value, errs);
        // (Only where the default makes the element's type: an element of an any pattern is any, its default
        // unread, tsc's getBindingElementTypeFromParentType returning the any parent type before
        // checkDeclarationInitializer pads it: `({ t: { n } = {} }) => {}` contextually typed by `(o: any) => void`)
        bool any_parent = my parent &&
            (ty_binding_type(current_checker, my parent, (LowerCtx){ .generic = true })->flags & TYF_ANY);
        if ((my tag == BINDING_ARRAY || my tag == BINDING_OBJECT) && binding_is_parameter(me) && !any_parent) {
            binding_add_padded_implicit_any_errors(me, default_type, errs);
        }
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
                    error_ctx_error_d(errs, at, 2502,
                        "'", my content_span, "' is referenced directly or indirectly in its own type annotation.");
                } else if (type == ty_intrinsic(current_checker, TY_CIRCULAR) &&
                    compiler_options()->no_implicit_any && !my has_explicit_type && !setter_parameter_of_getter(me)
                ) {
                    // (At tsc's symbol.valueDeclaration: a parameter's whole declaration, default included)
                    Span at = my content_span;
                    if (my symbol->is_parameter && my source && my source->tag == LAZY_TYPE_INITIALIZED_BINDING) {
                        InitializedBinding const* initialized = c_container_of(my source, InitializedBinding, lazy_type);
                        Expr const* value = initialized_binding_value(initialized);
                        if (value->tag != EXPR_IMPLICIT_ANY) at = span_between(my content_span, expr_outer_span(value));
                    }
                    error_ctx_error_d(errs, at, 7022,
                        "'", my content_span, "' implicitly has type 'any' because it does not have a type annotation and is referenced directly or indirectly in "
                        "its own initializer.");
                } else if (my parent && compiler_options()->no_implicit_any) {
                    // (tsc's reportErrorsFromWidening of a binding element)
                    Ty const* raw = ty_element_before_widening(current_checker, me);
                    if (raw && !binding_report_widening_errors_in_type(raw, errs)) {
                        error_ctx_error_d(errs, my content_span, 7031, "Binding element '", span_name_text(my content_span),
                            "' implicitly has an '", type, "' type.");
                    }
                }
            }
            break;

        case BINDING_ARRAY:
            Ty const* type = ty_binding_type(current_checker, me, (LowerCtx){ .generic = true });
            // (Not widened: `= null` is null, not any, without strictNullChecks. tsc's getTypeForBindingElementParent)
            Expr const* source = my parent ? my default_value : initializer;
            if (!my has_explicit_type && source && !(type->flags & TYF_NULLABLE)) {
                Ty const* assigned = ty_expr_type(current_checker, source);
                if (assigned->flags & TYF_NULLABLE) type = assigned;
            }
            // (Under a written type, without undefined if the initializer can't be undefined: tsc's
            // getBindingElementTypeFromParentType, like ty_binding_element's)
            if (!my parent && my has_explicit_type && source && compiler_options()->strict_null_checks &&
                !(type->flags & TYF_ANY) && !ty_can_equal_undefined(current_checker, ty_expr_type(current_checker, source))
            ) {
                type = ty_without_undefined(current_checker, type);
            }
            // (Nor null and undefined in an ambient parameter, which is just documentation: tsc's
            // getBindingElementTypeFromParentType. `declare function f([b]: [string] | null)` is fine)
            if (compiler_options()->strict_null_checks && my ambient && binding_is_parameter(me)) {
                type = ty_non_nullable(current_checker, type);
            }
            ty_iterated_type(current_checker, ITERATION_DESTRUCTURING, type, empty_pattern ? at : my content_span, errs);
            Ty const* parent = binding_element_parent_type(me, nullptr);
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
                    // (What isn't there at all is only possible without a lib: an Array without a number index
                    // signature, tsc's getIndexedAccessTypeOrUndefined of an array-like parent)
                    ty_check_property_access(current_checker, parent, ty_literal(current_checker, &index), span_null,
                        i->content_span, false, compiler_options()->no_lib && ty_extends_array(current_checker, parent), errs);
                }
                binding_typecheck_at(i, nullptr, span_null, true, false, bodiless, errs);
            }
            break;

        case BINDING_OBJECT: {
            Ty const* parent = binding_element_parent_type(me, my parent ? my default_value : initializer);
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
                } else if (i->target.rest && !ty_object_rest_source_valid(current_checker, binding_rest_source(me, initializer))) {
                    // (tsc's getTypeForBindingElement: isValidSpreadType of the parent)
                    error_ctx_error_d(errs, i->target.content_span, 2700, "Rest types may only be created from object types.");
                }
                if (i->private_name) {
                    // (tsgo's checkVariableLikeDeclaration)
                    error_ctx_grammar_error_d(errs, i->source_span, 18064, "Private identifiers cannot be used in destructuring patterns.");
                }
                // (A renaming in a signature without a body is only TS2842, nothing about the property: tsc's
                // checkVariableLikeDeclaration returns there, `type H = ({ x: a }: {}) => void`)
                if (bodiless && i->target.tag == BINDING_IDENTIFIER && !i->target.rest &&
                    (i->computed_key || !span_eq(i->source_span, i->target.content_span))
                ) {
                    continue;
                }
                if (parent && !i->computed_key && !i->private_name && !i->target.rest && !(i->target.default_value &&
                    parent->kind == TY_ANONYMOUS && parent->anonymous.object_literal)
                ) {
                    ty_check_property_access(current_checker, parent, ty_literal(current_checker, &i->source), span_null,
                        span_valid(i->source_span) ? i->source_span : i->target.content_span, false, true, errs);
                }
                if (i->computed_key) {
                    // (The name first, like tsc's checkComputedPropertyName: what it refers to may be the element itself)
                    ty_binding_computed_key_type(current_checker, i);
                    expr_typecheck(i->computed_key, errs);
                    // (tsc's checkBindingElement: checkComputedPropertyName)
                    ty_check_computed_name(current_checker, i->computed_key, i->source_span, false, errs);
                    // (tsc's getTypeForBindingElement: getIndexedAccessType with the name as the access node)
                    // (With a default, an object literal may lack it: AccessFlags.AllowMissing)
                    if (parent && !i->target.rest && !(i->target.default_value && parent->kind == TY_ANONYMOUS &&
                        parent->anonymous.object_literal)
                    ) {
                        Span key_span = expr_outer_span(i->computed_key);
                        // (The key the element's type was read with: `{}` for one that names the element itself)
                        Ty const* used = ty_binding_access_key(current_checker, i);
                        ty_check_destructuring_indexed_access(current_checker, parent, used ? used : expr_get_type(i->computed_key),
                            key_span, errs);
                    }
                    if (parent && !i->target.rest) {
                        ty_check_destructured_member(current_checker, parent, ty_regular(current_checker,
                            expr_get_type(i->computed_key)), span_valid(i->source_span) ? i->source_span : expr_outer_span(i->computed_key),
                            from_this, false, my access_site, errs);
                    }
                } else if (!i->target.rest && !i->private_name && (i->source.tag == VALUE_STRING || i->source.tag == VALUE_NUMBER)) {
                    Ty const* type = parent ? parent : unused_enabled() ?
                        ty_binding_type(current_checker, me, (LowerCtx){ .generic = true }) : nullptr;
                    if (type) {
                        ty_check_destructured_member(current_checker, type, ty_literal(current_checker, &i->source),
                            span_valid(i->source_span) ? i->source_span : i->target.content_span, from_this, false,
                            my access_site, errs);
                    }
                }
                if (i->target.rest && compiler_options()->target < SCRIPT_TARGET_ES2018 && !my ambient) {
                    ty_check_emit_helpers(current_checker, i->target.content_span, EMIT_HELPER_REST, errs);
                }
                binding_typecheck_at(&i->target, nullptr, span_null, true, false, bodiless, errs);
            }
            break;
        }
    }
}

void object_binding_item_drop(ObjectBindingItem* me) {
    if (my computed_key) {
        expr_drop(my computed_key);
        ast_delete(my computed_key);
    }
    value_drop(&my source);
    binding_drop(&my target);
}

const Expr initialized_binding_no_value = { .tag = EXPR_IMPLICIT_ANY, .lazy_type.tag = LAZY_TYPE_EVALUATED };

const InitializedBindingRare initialized_binding_no_rare = {0};

InitializedBindingRare* initialized_binding_rare_mut(InitializedBinding* me) {
    if (!my rare) my rare = ast_new(InitializedBindingRare, {0});
    return my rare;
}

void initialized_binding_drop(InitializedBinding* me) {
    if (my rare) {
        for (u32 i = 0; i < my rare->require_port_count; i++) port_item_drop(&my rare->require_ports[i]);
        ast_free(my rare->require_ports, my rare->require_port_count * c_sizeof(PortItem));
        ast_delete(my rare);
    }
    binding_drop(&my binding);
    if (my value) {
        expr_drop(my value);
        ast_delete(my value);
    }
    lazy_type_drop(&my lazy_type);
}

void initialized_binding_add_names(InitializedBinding* me, Scope* scope, BindingAddNamesProps props) {
    lazy_type_bind(&my lazy_type, LAZY_TYPE_INITIALIZED_BINDING);
    my abstract_literals = props.abstract_literals;
    // `export const x = require("m")` is a variable (tsgo's isVariableDeclarationInitializedToRequire: not exported)
    if (initialized_binding_rare(me)->require_port_count && props.exported) {
        for (u32 i = 0; i < my rare->require_port_count; i++) port_item_drop(&my rare->require_ports[i]);
        ast_free(my rare->require_ports, my rare->require_port_count * c_sizeof(PortItem));
        my rare->require_ports = nullptr;
        my rare->require_port_count = 0;
        Expr* call = initialized_binding_value(me);
        if (call->tag == EXPR_CALL && call->call->callable.tag == EXPR_IDENTIFIER) call->call->callable.identifier.require_call = 1;
    }
    if (!initialized_binding_rare(me)->require_port_count) {
        binding_add_names(&my binding, &my lazy_type, scope, props);
        return;
    }
    // The names are aliases (see require_ports). Declared in the function
    // or file around a block, like a `var` (tsc's declareSymbolAndAddToSymbolTable puts aliases in the container's
    // locals: two `const X = require()` in different blocks are TS2300, microsoft/TypeScript#41697)
    while (scope->parent && !scope_is_var_container(scope)) scope = scope->parent;
    my binding.source = &my lazy_type;
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
    if (callee->tag == EXPR_PROPERTY && span_equals(property_key_inner_span(callee->property), "for")) callee = &callee->property->expr;
    if (callee->tag != EXPR_IDENTIFIER || !csview_equals(span_sv(callee->inner_span), "Symbol")) return;
    CallRare* rare = call_rare_mut(call);
    if (!rare->unique_symbol) rare->unique_symbol = ast_malloc(sizeof(UniqueSymbolDecl));
    *rare->unique_symbol = decl;
}

void initialized_binding_lookup_names(InitializedBinding* me, Scope* names, ErrorCtx* errs) {
    if (my binding.tag == BINDING_IDENTIFIER && my binding.symbol && my binding.symbol->is_const) {
        unique_symbol_decl_mark(
            (UniqueSymbolDecl){ .key = my binding.symbol, .owner = my binding.symbol },
            binding_type(&my binding), initialized_binding_value(me)
        );
    }
    if (initialized_binding_rare(me)->require_port_count) module_ref_report_require(initialized_binding_rare(me)->require_ports[0].module_ref, names, errs);
    InitializedBinding const* outer = scope_flow(names)->initializing;
    scope_flow(names)->initializing = me;
    binding_lookup_names(&my binding, names, errs);
    scope_flow(names)->contextual_depth += my binding.has_explicit_type;
    // (tsc's getNameOfSymbolAsWritten: an anonymous function is named by the variable it initializes)
    if (my binding.tag == BINDING_IDENTIFIER && initialized_binding_value(me)->tag == EXPR_FUNCTION) function_impl_set_variable_name(initialized_binding_value(me)->function, my binding.content_span);
    if (my binding.tag == BINDING_IDENTIFIER && my binding.symbol && initialized_binding_value(me)->tag == EXPR_FUNCTION &&
        !expr_is_perenthisized(initialized_binding_value(me)) && !binding_is_parameter(&my binding) &&
        (names->kind == SCOPE_SCRIPT || names->kind == SCOPE_MODULE || names->kind == SCOPE_NAMESPACE ||
            names->kind == SCOPE_GLOBAL_AUGMENTATION)
    ) {
        function_impl_rare_mut(initialized_binding_value(me)->function)->top_level_variable = my binding.symbol;
    }
    // (The context root of its function expressions, FunctionImpl.context_root: the walk checks the initializer)
    uintptr_t outer_root = scope_flow(names)->context_root;
    scope_flow(names)->context_root = context_root_make(me, CONTEXT_ROOT_BINDING);
    if (my value) expr_lookup_names(my value, names, errs);
    scope_flow(names)->context_root = outer_root;
    if (my binding.has_explicit_type && my value && my value->tag == EXPR_UNARY_OP && my value->unary_op->op == UNARY_OP_YIELD) {
        my value->unary_op->declared_context = binding_type(&my binding);
    }
    scope_flow(names)->contextual_depth -= my binding.has_explicit_type;
    scope_flow(names)->initializing = outer;
    if (initialized_binding_value(me)->tag != EXPR_IMPLICIT_ANY) binding_flow_assign(&my binding, names);
}

void initialized_binding_lookup_names_parameter(InitializedBinding* me, Scope* names, bool value_unchecked, ErrorCtx* errs) {
    FlowBuilder* flow = scope_flow(names);
    Binding const* saved = flow->parameter_initializing;
    Scope const* saved_scope = flow->parameter_function_scope;
    flow->parameter_initializing = &my binding;
    flow->parameter_function_scope = names->parent; // `names` is the parameter list's own scope
    binding_lookup_names(&my binding, names, errs);
    flow->contextual_depth += my binding.has_explicit_type;
    uintptr_t outer_root = flow->context_root;
    flow->context_root = context_root_make(me, CONTEXT_ROOT_BINDING);
    if (my value) expr_lookup_names_initializer(my value, names, value_unchecked ? binder_discarded_errs(names->binder) : errs);
    flow->context_root = outer_root;
    flow->contextual_depth -= my binding.has_explicit_type;
    flow->parameter_initializing = saved;
    flow->parameter_function_scope = saved_scope;
}

// A reference doesn't ask for its contextual type (tsc's checkIdentifier and checkPropertyAccessExpression, but for
// getNarrowableTypeForReference's hasContextualTypeWithNoGenericTypes when its type has a type variable with a union
// constraint, not any generic one: `Opts<T>` doesn't): the type a pattern
// implies isn't made then, nor are its defaults checked without a context (`const { f = (d) => d } = o` takes f's
// contextual type from o's property, not from an any of the pattern's)
static bool initializer_ignores_context(Checker* checker, Expr const* value) {
    // A type assertion or `satisfies` gives its operand the written type as context, never the outer one: what the
    // pattern implies is never asked for, its defaults aren't checked without context first (tsc's
    // getContextualTypeForInitializerExpression is only asked on demand; kirka's `{ ok = (v) => v } = {} as {...}`)
    if (value->tag == EXPR_AS_TYPE) return true;
    // A call asks only to infer its type arguments (tsc's inferTypeArguments): `const { a = (v) => v } = mk()` with a
    // non-generic mk gives `v` the contextual type of mk's property, never the pattern's
    if (value->tag == EXPR_CALL) return !ty_call_may_ask_context(checker, value->call);
    if (value->tag != EXPR_IDENTIFIER && value->tag != EXPR_PROPERTY) return false;
    if (value->tag == EXPR_PROPERTY && value->property->form == PROPERTY_EXPR_BRACKET) return false;
    Ty const* type = ty_expr_type(checker, (Expr*)value);
    return !(type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) || !ty_reference_uses_context(checker, value);
}

// The initializer, checked with the type annotation (if any) as its contextual type
static Ty const* initialized_binding_check_value(InitializedBinding* me) {
    Checker* checker = current_checker;
    Ty const* contextual = nullptr;
    if (my binding.has_explicit_type) {
        contextual = ty_from_node(checker, binding_type(&my binding), (LowerCtx){ .generic = true });
    } else if (binding_is_parameter(&my binding) && ty_contextual_parameter_type(checker, me)) {
        // A parameter's contextual type (an immediately invoked function's argument: tsc's
        // getContextuallyTypedParameterType), before what its pattern implies
        contextual = ty_contextual_parameter_type(checker, me);
    } else if (((my binding.tag == BINDING_OBJECT && vec_object_binding_item_size(&my binding.object)) ||
        (my binding.tag == BINDING_ARRAY && vec_binding_size(&my binding.array))) &&
        !initializer_ignores_context(checker, initialized_binding_value(me))
    ) {
        // Else what a pattern implies (tsc's getContextualTypeForInitializerExpression): `[1, "a"]` is a tuple there
        contextual = ty_binding_pattern_type(checker, &my binding, true);
        // (An array literal asks for it, tsc's checkArrayLiteral; a name doesn't)
        Expr const* value = initialized_binding_value(me);
        if (value->tag == EXPR_ARRAY || value->tag == EXPR_OBJECT) ty_report_pattern_iterable(checker, &my binding);
    }
    return ty_check_expr(checker, initialized_binding_value(me), contextual);
}

// The initializer checked as the declaration's evaluation checks it (initialized_binding_evaluate_assigned_type()), for a
// function expression in it that needs its contextual type first (ty_settle_function_context()): memoized, so the
// declaration's own check finds it done
void initialized_binding_settle_value(InitializedBinding* me) {
    Checker* checker = current_checker;
    if (!my value) return;
    FlowIsolation isolation = ty_flow_isolate(checker);
    struct OuterInference* outer = checker->outer_inference;
    if (!binding_is_parameter(&my binding)) checker->outer_inference = nullptr;
    initialized_binding_check_value(me);
    checker->outer_inference = outer;
    ty_flow_restore(checker, isolation);
}

// `let x;`, `let x = null` and `let x = []` under noImplicitAny (JavaScript too, nothing more: tsgo): the type comes
// from what is assigned (and pushed), as control flow sees it. tsc's autoType and autoArrayType in
// getWidenedTypeForVariableLikeDeclaration: TY_AUTO, ty_auto_array(), else null
static Ty const* auto_type(InitializedBinding const* me) {
    if (my binding.has_explicit_type || my binding.tag != BINDING_IDENTIFIER || !compiler_options()->no_implicit_any) {
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
    if (initialized_binding_rare(me)->late_setter) return function_impl_late_getter(initialized_binding_rare(me)->late_setter);
    if (!initialized_binding_rare(me)->getter) return nullptr;
    Expr const* key = initialized_binding_rare(me)->getter_key;
    if (!key || key->tag == EXPR_LITERAL) return initialized_binding_rare(me)->getter;
    Ty const* type = ty_expr_type(current_checker, (Expr*)key);
    return type->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL) ? initialized_binding_rare(me)->getter : nullptr;
}

// Whether the initializer's type `init` is that of `[]` in an unannotated JavaScript declaration, which makes it any[]
bool initialized_binding_js_empty_array(InitializedBinding const* me, Ty const* init) {
    SourceFile const* file = span_source(my binding.content_span);
    return file && file->is_js && !my binding.has_explicit_type && my binding.tag == BINDING_IDENTIFIER &&
        ty_is_empty_array_literal_type(current_checker, init);
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
    // (Which sees the loops in progress: `const last = route.history.at(-1)` in a loop that assigns `route = { ...route,
    // last }` is narrowed by the loop's incomplete type, not walked through the assignment back to `last`)
    Ty const* ret = ty_quick_expr_type(checker, initialized_binding_value(me), false);
    if (!ret) {
        FlowIsolation isolation = ty_flow_isolate(checker); // (tsc's checkExpressionCached)
        // A variable's initializer isn't an argument of the call inferring now (tsc finds that call among the node's
        // ancestors, getInferenceContext): whoever asks for the variable's type, its contextual types are its own. A
        // parameter's default may be inside an argument
        struct OuterInference* outer = checker->outer_inference;
        if (!binding_is_parameter(&my binding)) checker->outer_inference = nullptr;
        ret = initialized_binding_check_value(me);
        checker->outer_inference = outer;
        ty_flow_restore(checker, isolation);
    }
    if (my binding.tag == BINDING_OBJECT || my binding.tag == BINDING_ARRAY) ret = ty_pad_initializer_type(checker, ret, &my binding);
    Symbol const* symbol = my binding.tag == BINDING_IDENTIFIER ? my binding.symbol : nullptr;
    // A pattern's elements are widened one by one (ty_binding_assigned_type()), the object they come from isn't (tsc's
    // getTypeForBindingElementParent): `{ a, b } = options || {}` reads the members of `{ a: A; b: B } | {}`, where the
    // widened `{}` would absorb the other member
    // (A parameter's pattern is its declared type too, which is widened)
    bool pattern_source = my binding.tag != BINDING_IDENTIFIER && !binding_is_parameter(&my binding);
    // `x = []` in JavaScript is any[] (tsc's widenTypeInferredFromInitializer; reportImplicitAny says so, see
    // initialized_binding_js_empty_array())
    if (initialized_binding_js_empty_array(me, ret)) return ty_array(checker, ty_intrinsic(checker, TY_ANY), false);
    if (!my binding.has_explicit_type && !pattern_source) {
        ret = ty_widened_for_declaration(checker, ret, my binding.tag == BINDING_IDENTIFIER && my abstract_literals);
        // A unique symbol made for another declaration is only a symbol here (tsc's widenTypeForVariableLikeDeclaration)
        if ((ret->flags & TYF_UNIQUE_ES_SYMBOL) && ret->literal.unique->key != symbol) {
            ret = ty_intrinsic(checker, TY_ES_SYMBOL);
        }
    }
    return ret;
}

// Whether an object literal's property is declared by the literal itself rather than spread into it (tsc's
// reportWideningErrorsInType: the valueDeclaration whose parent is the literal)
static bool declared_in_literal(Ty const* ty, Prop const* prop) {
    Expr const* literal = ty->anonymous.literal_expr;
    if (!literal || literal->tag != EXPR_OBJECT) return true;
    char const* at = prop->decl->decl_spans[0].buf;
    for_each (i, vec_object_expr_item, &literal->object) {
        if (i->tag != OBJECT_ITEM_SPREAD && at >= i->span.buf && at < i->span.buf + i->span.size) return true;
    }
    return false;
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
        // (tsc's `errorReported ||= ...`: the members after the first one that reports aren't looked at)
        for (u32 i = 0; i < ty->composite.types.len && !reported; i++) reported = report_widening_errors_in_type(checker, ty->composite.types.data[i], errs, depth + 1);
    } else if (ty->kind == TY_REFERENCE && (ty_is_array(checker, ty) || ty_tuple_shape(ty))) {
        TyList args = ty_reference_args(checker, ty);
        for (u32 i = 0; i < args.len && !reported; i++) reported = report_widening_errors_in_type(checker, args.data[i], errs, depth + 1);
    } else if (ty->kind == TY_ANONYMOUS && ty->anonymous.object_literal) {
        Members const* members = ty_members(checker, ty);
        for (u32 i = 0; i < members->prop_count; i++) {
            Prop const* prop = members->props[i];
            if (prop->decl->function) continue;
            Ty const* t = prop_type(checker, prop);
            if (!(t->xflags & TYX_CONTAINS_WIDENING)) continue;
            bool inner = report_widening_errors_in_type(checker, t, errs, depth + 1);
            // (At the property's declaration, `a: null` and not just its name: tsc's valueDeclaration, a spread one's too)
            if (!inner && prop->decl->decl_span_count && span_source(prop->decl->decl_spans[0]) && declared_in_literal(ty, prop)) {
                cstr name = ty_prop_name_cstr(checker, prop->name);
                cstr type = ty_to_cstr(checker, ty_widened_nullable(checker, t));
                error_ctx_error_d(errs, prop->decl->decl_spans[0], 7018, "Object literal's property '", name, "' implicitly has an '", type, "' type.");
                c_drop(cstr, &name, &type);
                inner = true;
            }
            // (tsc's `errorReported = ...`: the last property that has something to widen decides)
            reported = inner;
        }
    }
    return reported;
}

bool binding_report_widening_errors_in_type(Ty const* ty, ErrorCtx* errs) {
    return report_widening_errors_in_type(current_checker, ty, errs, 0);
}

static void report_widened_type(Binding const* binding, Ty const* ty, Span parameter, ErrorCtx* errs);

// `parameter`: the whole parameter when `me` is one (tsc reports at the declaration, a variable's at its name)
static void report_errors_from_widening(InitializedBinding* me, Span parameter, ErrorCtx* errs) {
    Checker* checker = current_checker;
    if (!compiler_options()->no_implicit_any) return;
    if (my binding.has_explicit_type || initialized_binding_value(me)->tag == EXPR_IMPLICIT_ANY || my binding.tag != BINDING_IDENTIFIER) return;
    if (auto_type(me) || my is_rest_parameter) return;
    Symbol const* symbol = my binding.symbol;
    if (!symbol) return;
    if (symbol->is_parameter && ty_contextual_parameter_type(checker, me)) return;
    Ty const* ty = ty_expr_type(checker, initialized_binding_value(me));
    if (compiler_options()->strict_null_checks) {
        // `x = []` in JavaScript is any[] (tsc's widenTypeInferredFromInitializer and its reportImplicitAny; without
        // strictNullChecks the widening below says the same)
        if (!initialized_binding_js_empty_array(me, ty) || initialized_binding_getter(me)) return;
        error_ctx_error_d(errs, span_valid(parameter) ? parameter : my binding.content_span, symbol->is_parameter ? 7006 : 7005,
            symbol->is_parameter ? "Parameter '" : "Variable '", span_name_text(my binding.content_span), "' implicitly has an 'any[]' type.");
        return;
    }
    report_widened_type(&my binding, ty, parameter, errs);
}

// TS7018 inside the type, else TS7005/TS7006 at the declaration (tsc's reportErrorsFromWidening)
static void report_widened_type(Binding const* binding, Ty const* ty, Span parameter, ErrorCtx* errs) {
    Checker* checker = current_checker;
    if (!(ty->xflags & TYX_CONTAINS_WIDENING)) return;
    if (report_widening_errors_in_type(checker, ty, errs, 0)) return;
    bool is_parameter = binding->symbol->is_parameter;
    cstr type = ty_to_cstr(checker, ty_widened_nullable(checker, ty));
    error_ctx_error_d(errs, span_valid(parameter) ? parameter : binding->content_span, is_parameter ? 7006 : 7005,
        is_parameter ? "Parameter '" : "Variable '", span_name_text(binding->content_span), "' implicitly has an '", type, "' type.");
    cstr_drop(&type);
}

// A for-of variable takes the iterated type, which widens like an initializer's (tsc's
// widenTypeForVariableLikeDeclaration): `for (const x of [null])` is TS7005, `of [{ a: [] }]` TS7018
void binding_report_iterated_widening(Binding const* me, Ty const* element, ErrorCtx* errs) {
    if (!compiler_options()->no_implicit_any || compiler_options()->strict_null_checks) return;
    if (my has_explicit_type || my tag != BINDING_IDENTIFIER || !my symbol || !element) return;
    report_widened_type(me, element, span_null, errs);
}

// The declaration's type takes a call's return type from its callee checked not to be nullish
// (getQuickTypeOfExpression's checkNonNullExpression): a possibly undefined callee is TS2532/TS18048 besides the
// call's own TS2722
static void check_quick_callee(Expr const* quick, ErrorCtx* errs) {
    if (quick->tag == EXPR_UNARY_OP && quick->unary_op->op == UNARY_OP_AWAIT) quick = &quick->unary_op->expr;
    if (quick->tag != EXPR_CALL || quick->call->uses_new || expr_is_optional_chain(quick) || expr_is_require_call(quick)) return;
    Expr const* callee = &quick->call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER) return;
    ty_check_non_null_type(current_checker, ty_expr_type(current_checker, callee), callee, errs);
}

// The computed names of a pattern's elements, in order (tsc's checkVariableLikeDeclaration checks the elements, each
// one's computed name first, before the initializer with the pattern as its contextual type)
static void binding_check_computed_names_first(Binding const* me) {
    if (my tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &my object) {
            if (i->computed_key) ty_binding_computed_key_type(current_checker, i);
            binding_check_computed_names_first(&i->target);
        }
    } else if (my tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &my array) binding_check_computed_names_first(i);
    }
}

// The first name of a pattern, depth first: the first element whose type tsc's checkVariableLikeDeclaration asks for
static Binding const* binding_first_name(Binding const* me) {
    if (my tag == BINDING_IDENTIFIER) return my symbol ? me : nullptr;
    if (my tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &my object) {
            Binding const* found = binding_first_name(&i->target);
            if (found) return found;
        }
    } else if (my tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &my array) {
            Binding const* found = binding_first_name(i);
            if (found) return found;
        }
    }
    return nullptr;
}

// The annotation's walk (tsc's checkSourceElement of the type node). A union or intersection is resolved at it
// (checkUnionOrIntersectionType: TS2590 of a cross product goes to the type node, not the declaration's name)
static void binding_annotation_typecheck(Binding const* me, ErrorCtx* errs) {
    if (!my has_explicit_type) return;
    TypeNode const* type = binding_type(me);
    type_typecheck(type, errs);
    if ((type->tag == TYPE_UNION || type->tag == TYPE_INTERSECTION) && my has_explicit_type_span) {
        CurrentNode saved = ty_enter_node(current_checker, binding_explicit_type_span(me));
        ty_of_node(type);
        ty_leave_node(current_checker, saved);
    }
}

void initialized_binding_typecheck(InitializedBinding* me, ErrorCtx* errs) {
    // `const x = require("m")`: an alias, which is all tsc checks
    // (checkVariableLikeDeclaration: checkAliasSymbol and return)
    if (initialized_binding_rare(me)->require_port_count) {
        // (`const { a } = require()`: tsc's getExternalModuleMember of each name. Not `require().a.b`)
        for (u32 i = 0; my binding.tag == BINDING_OBJECT && i < initialized_binding_rare(me)->require_port_count; i++) {
            port_item_check_export_equals_property(&initialized_binding_rare(me)->require_ports[i], errs);
        }
        for (u32 i = 0; i < initialized_binding_rare(me)->require_port_count; i++) {
            port_item_check_js_require_type(&initialized_binding_rare(me)->require_ports[i], errs);
        }
        // (A destructuring declaration has no alias symbol of its own: tsc checks its initializer, the `require()`
        // call against a declared `require` function's signature)
        if (my binding.tag == BINDING_OBJECT) expr_typecheck(initialized_binding_value(me), errs);
        return;
    }
    // tsc's checkSourceElement of the VariableDeclaration: what goes wrong deep inside is reported at its name, and
    // checkVariableLikeDeclaration asks for the variable's type first, which takes a call's return type without
    // checking the call (getQuickTypeOfExpression): an instantiation limit it runs into is the declaration's (TS2589)
    Checker* checker = current_checker;
    CurrentNode saved = ty_enter_node(checker, my binding.content_span);
    // (The annotation is walked first, which makes the types of the literals in it: tsc's checkVariableLikeDeclaration)
    binding_annotation_typecheck(&my binding, errs);
    if (my binding.tag == BINDING_IDENTIFIER && my binding.symbol && initialized_binding_value(me)->tag == EXPR_CALL) {
        ty_type_of_symbol(checker, my binding.symbol);
    }
    binding_check_computed_names_first(&my binding);
    // An annotated pattern's elements are checked before its initializer, and under strictNullChecks the first one's
    // type asks for the initializer's (tsc's getBindingElementTypeFromParentType, whether it can be undefined): a
    // function in it with a contextual signature infers its return type then (contextuallyCheckFunctionExpression...),
    // and a name read there is circular, `const [d, e]: [number, (n: number) => number] = [1, (n) => e(n) + d]`, TS7022
    if (my binding.tag != BINDING_IDENTIFIER && my binding.has_explicit_type && compiler_options()->strict_null_checks &&
        initialized_binding_value(me)->tag != EXPR_IMPLICIT_ANY
    ) {
        Binding const* first = binding_first_name(&my binding);
        if (first) ty_type_of_symbol(checker, first->symbol);
    }
    bool entered = ty_context_root_enter(me);
    initialized_binding_check_value(me);
    ty_context_root_leave(entered);
    // (Only when the initializer types the declaration: an annotated one never asks for its quick type)
    if (!my binding.has_explicit_type) check_quick_callee(initialized_binding_value(me), errs);
    expr_typecheck(initialized_binding_value(me), errs);
    Expr const* initializer = initialized_binding_value(me)->tag == EXPR_IMPLICIT_ANY ? nullptr : initialized_binding_value(me);
    binding_typecheck_at(&my binding, initializer, span_null, initializer, true, false, errs);
    report_errors_from_widening(me, span_null, errs);
    ty_leave_node(checker, saved);
}

void initialized_binding_typecheck_parameter(InitializedBinding* me, Span span, bool bodiless, ErrorCtx* errs) {
    binding_annotation_typecheck(&my binding, errs);
    binding_check_computed_names_first(&my binding);
    bool entered = ty_context_root_enter(me);
    initialized_binding_check_value(me);
    ty_context_root_leave(entered);
    // (Only when the initializer types the declaration: an annotated one never asks for its quick type)
    if (!my binding.has_explicit_type) check_quick_callee(initialized_binding_value(me), errs);
    expr_typecheck(initialized_binding_value(me), errs);
    Expr const* initializer = initialized_binding_value(me)->tag == EXPR_IMPLICIT_ANY ? nullptr : initialized_binding_value(me);
    binding_typecheck_at(&my binding, initializer, span, initializer, true, bodiless, errs);
    report_errors_from_widening(me, span, errs);
    if (!initializer || my binding.has_explicit_type) return;
    // An unannotated parameter typed by its contextual signature: the initializer is assigned to that type, unless it
    // was the initializer's type instead (tsc's assignContextualParameterTypes: when the contextual type is assignable
    // to the widened initializer but not the other way around)
    Checker* checker = current_checker;
    Ty const* contextual = ty_contextual_parameter_type(checker, me);
    if (!contextual) return;
    Ty const* value = ty_expr_type(checker, initializer);
    // (A binding pattern's initializer is checked against the contextual type itself: tsc's checkVariableLikeDeclaration
    // asks getWidenedTypeForVariableLikeDeclaration there, not the symbol's assigned type)
    Ty const* unwidened = my binding.tag != BINDING_IDENTIFIER ? ty_contextual_parameter_type_unwidened(checker, me) : nullptr;
    if (unwidened && unwidened != contextual) {
        if (!ty_is_related(checker, value, unwidened, TY_RELATION_ASSIGNABLE)) {
            ty_check_assignable_expr(checker, value, unwidened, span, initializer, errs);
        }
        return;
    }
    Ty const* type = ty_binding_type(checker, &my binding, (LowerCtx){ .generic = true });
    if (ty_is_related(checker, value, type, TY_RELATION_ASSIGNABLE)) return;
    if (ty_is_related(checker, type, ty_widened_for_declaration(checker, value, false), TY_RELATION_ASSIGNABLE)) return;
    ty_check_assignable_expr(checker, value, type, span, initializer, errs);
}
