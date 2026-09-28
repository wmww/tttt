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

void binding_drop(Binding* me) {
    switch (my tag) {
        case BINDING_NOOP: break;
        case BINDING_IDENTIFIER: break;
        case BINDING_ARRAY: vec_binding_drop(&my array); break;
        case BINDING_OBJECT: vec_object_binding_item_drop(&my object); break;
    }
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
                props.full = span_valid(i->element_span) ? i->element_span : i->content_span;
                i->parent = me;
                i->parent_index = (u32)i_index;
                binding_add_names(i, nullptr, scope, props);
            }
            break;

        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                // The element starts at its property name (`{ x: a }` declares `a` at `x`)
                props.full = i->target.rest ? i->target.element_span :
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
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            // The flow container the variable belongs to: elsewhere it is assumed to be initialized
            if (my symbol && !my symbol->flow_start && names->flow) my symbol->flow_start = names->flow->start;
            binding_check_var_not_shadowed(me, names, errs);
            break;

        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) {
                binding_lookup_names(i, names, errs);
            }
            break;

        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                if (i->computed_key) {
                    expr_lookup_names(i->computed_key, names, errs);
                }
                binding_lookup_names(&i->target, names, errs);
            }
            break;
    }

    if (my default_value) {
        // In a parameter's pattern the default is the element's own (tsc's associatedDeclarationForContainingInitializer)
        Binding const* saved = names->flow ? names->flow->parameter_initializing : nullptr;
        if (saved) names->flow->parameter_initializing = me;
        expr_lookup_names_initializer(my default_value, names, errs);
        if (saved) names->flow->parameter_initializing = saved;
    }
    if (my has_explicit_type) {
        type_lookup_names(&my explicit_type, names, errs);
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
void binding_add_implicit_any_type_errors(Binding const* me, bool toplevel, ErrorCtx* errs) {
    if (!toplevel && my default_value) return;
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            error_ctx_error_d(
                errs, my content_span, toplevel ? 7006 : 7031,
                toplevel ? "Parameter" : "Binding element", " '", my content_span, "' implicitly has an 'any' type."
            );
            break;

        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) {
                binding_add_implicit_any_type_errors(i, false, errs);
            }
            break;

        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                binding_add_implicit_any_type_errors(&i->target, false, errs);
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
    Symbol const* symbol = my symbol;
    SymbolDecl const* first = nullptr;
    // (The namespace an expando makes of a name isn't its value declaration: tsc's setValueDeclaration)
    for (u32 i = 0; i < symbol->decl_count && !first; i++) {
        if (symbol->decls[i].flags & SYMBOL_VALUE && symbol->decls[i].kind != SYMBOL_DECL_EXPANDO) first = &symbol->decls[i];
    }
    if (!first || first->node == me) return;
    Checker* checker = current_checker;
    Ty const* type = ty_type_of_symbol(checker, symbol);
    Ty const* declared = ty_binding_type(checker, me, (LowerCtx){ .generic = true });
    Ty const* error = ty_intrinsic(checker, TY_ERROR);
    Ty const* circular = ty_intrinsic(checker, TY_CIRCULAR);
    if (type == error || declared == error || type == circular || declared == circular ||
        ty_is_related(checker, type, declared, TY_RELATION_IDENTITY)
    ) {
        return;
    }
    cstr type_str = ty_to_cstr(checker, type);
    cstr declared_str = ty_to_cstr(checker, declared);
    Error* e = error_ctx_error_d(errs, my content_span, 2403,
        "Subsequent variable declarations must have the same type.  Variable '", my content_span, "' must be of type '",
        type_str, "', but here has type '", declared_str, "'.");
    error_add_related_d(e, first->name, 6203, "'", my content_span, "' was also declared here.");
    c_drop(cstr, &type_str, &declared_str);
}

// The type a pattern's elements are taken from, as far as reporting what it lacks goes (tsc's
// getBindingElementTypeFromParentType): null when nothing is reported (any, or nothing an element could be missing from).
// An object pattern lacks every property of unknown (TS2339), an array pattern reports it once (TS2461)
static Ty const* element_parent_type(Binding const* me) {
    Checker* checker = current_checker;
    Ty const* type = ty_binding_type(checker, me, (LowerCtx){ .generic = true });
    if (type->flags & TYF_ANY || (type->flags & TYF_UNKNOWN && my tag == BINDING_ARRAY)) return nullptr;
    // (tsc takes the non-nullable part: `{}` without strictNullChecks, unknown itself with them)
    if (type->flags & TYF_UNKNOWN) return compiler_options()->strict_null_checks ? type : ty_empty_object_type(checker);
    // (What can be null or undefined is reported about the whole pattern)
    type = ty_non_nullable(checker, type);
    if (type->flags & TYF_NEVER) return nullptr;
    return type;
}

void binding_typecheck(Binding* me, Expr const* initializer, ErrorCtx* errs) {
    if (my has_explicit_type) type_typecheck(&my explicit_type, errs);
    // (A catch variable's annotation isn't assigned anything: TS1196 is its only check)
    if (my has_explicit_type && (my parent || my source)) {
        ty_check_assignable_expr(current_checker, ty_binding_assigned_type(current_checker, me),
            ty_of_node(&my explicit_type), my content_span, initializer, errs);
    }

    if (my default_value) {
        Ty const* default_type = ty_binding_default_type(current_checker, me);
        expr_typecheck(my default_value, errs);
        if (my tag == BINDING_ARRAY && binding_is_parameter(me)) binding_add_padded_implicit_any_errors(me, default_type, errs);
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
                }
            }
            break;

        case BINDING_ARRAY:
            if (compiler_options()->target < SCRIPT_TARGET_ES2015 && compiler_options()->downlevel_iteration && !my ambient) {
                ty_check_emit_helpers(current_checker, my content_span, EMIT_HELPER_READ, errs);
            }
            Ty const* type = ty_binding_type(current_checker, me, (LowerCtx){ .generic = true });
            ty_iterated_type(current_checker, ITERATION_DESTRUCTURING, type, my content_span, errs);
            Ty const* parent = element_parent_type(me);
            for_each (i, vec_binding, &my array) {
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
            Ty const* parent = element_parent_type(me);
            for_each (i, vec_object_binding_item, &my object) {
                if (parent && !i->computed_key && !i->target.rest && !(i->target.default_value &&
                    parent->kind == TY_ANONYMOUS && parent->anonymous.object_literal)
                ) {
                    ty_check_property_access(current_checker, parent, ty_literal(current_checker, &i->source), span_null,
                        span_valid(i->source_span) ? i->source_span : i->target.content_span, false, true, errs);
                }
                if (i->computed_key) {
                    expr_typecheck(i->computed_key, errs);
                    // (tsc's getTypeForBindingElement: getIndexedAccessType with the name as the access node)
                    if (parent && !i->target.rest) {
                        Span key_span = i->computed_key->outer_span;
                        ty_check_indexed_access_type(current_checker, parent, expr_get_type(i->computed_key), key_span,
                            key_span, errs);
                    }
                } else if (!i->target.rest && i->source.tag == VALUE_STRING && unused_enabled()) {
                    Ty const* type = ty_binding_type(current_checker, me, (LowerCtx){ .generic = true });
                    ty_mark_destructured_member(current_checker, type, cstr_sv(&i->source.string));
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

void initialized_binding_drop(InitializedBinding* me) {
    for (u32 i = 0; i < my require_port_count; i++) port_item_drop(&my require_ports[i]);
    free(my require_ports);
    binding_drop(&my binding);
    expr_drop(&my value);
    lazy_type_drop(&my lazy_type);
}

void initialized_binding_add_names(InitializedBinding* me, Scope* scope, BindingAddNamesProps props) {
    lazy_type_bind(&my lazy_type, LAZY_TYPE_INITIALIZED_BINDING);
    my abstract_literals = props.abstract_literals;
    if (!my require_port_count) {
        binding_add_names(&my binding, &my lazy_type, scope, props);
        return;
    }
    // The names are aliases (see require_ports), exported like `export import x = require()`. Declared in the function
    // or file around a block, like a `var` (tsc's declareSymbolAndAddToSymbolTable puts aliases in the container's
    // locals: two `const X = require()` in different blocks are TS2300, microsoft/TypeScript#41697)
    while (scope->parent && !scope_is_var_container(scope)) scope = scope->parent;
    my binding.source = &my lazy_type;
    for (u32 i = 0; i < my require_port_count; i++) my require_ports[i].export_import = props.exported;
    if (my binding.tag == BINDING_IDENTIFIER) {
        scope_add_port(scope, &my require_ports[0]);
        my binding.symbol = my require_ports[0].symbol;
        return;
    }
    for_each (i, vec_object_binding_item, &my binding.object) {
        i->target.parent = &my binding;
        i->target.parent_item = i;
        PortItem* port = &my require_ports[i_index];
        scope_add_port(scope, port);
        i->target.symbol = port->symbol;
    }
}

// A `const` declaration of a variable can have a unique symbol type: its annotation and a `Symbol()` initializer say so
void unique_symbol_decl_mark(UniqueSymbolDecl decl, TypeNode* annotation, Expr* initializer) {
    if (annotation && annotation->tag == TYPE_UNIQUE_SYMBOL) annotation->unique_symbol = decl;
    if (initializer && initializer->tag == EXPR_CALL && !initializer->call->uses_new) initializer->call->unique_symbol = decl;
}

void initialized_binding_lookup_names(InitializedBinding* me, Scope* names, ErrorCtx* errs) {
    if (my binding.tag == BINDING_IDENTIFIER && my binding.symbol && my binding.symbol->is_const) {
        unique_symbol_decl_mark(
            (UniqueSymbolDecl){ .key = my binding.symbol, .owner = my binding.symbol },
            &my binding.explicit_type, &my value
        );
    }
    if (my require_port_count) module_ref_report(my require_ports[0].module_ref, names, errs);
    InitializedBinding const* outer = names->flow->initializing;
    names->flow->initializing = me;
    binding_lookup_names(&my binding, names, errs);
    names->flow->contextual_depth += my binding.has_explicit_type;
    if (my binding.tag == BINDING_IDENTIFIER && my value.tag == EXPR_FUNCTION && !span_valid(my value.function->arrow_span) &&
        names->file && names->file->is_js
    ) {
        my value.function->class_symbol = my binding.symbol;
    }
    // (tsc's getNameOfSymbolAsWritten: an anonymous function is named by the variable it initializes)
    if (my binding.tag == BINDING_IDENTIFIER && my value.tag == EXPR_FUNCTION) my value.function->variable_name = my binding.content_span;
    expr_lookup_names(&my value, names, errs);
    names->flow->contextual_depth -= my binding.has_explicit_type;
    names->flow->initializing = outer;
    if (my value.tag != EXPR_IMPLICIT_ANY) binding_flow_assign(&my binding, names);
}

void initialized_binding_lookup_names_parameter(InitializedBinding* me, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    Binding const* saved = flow->parameter_initializing;
    Scope const* saved_scope = flow->parameter_function_scope;
    flow->parameter_initializing = &my binding;
    flow->parameter_function_scope = names->parent; // `names` is the parameter list's own scope
    binding_lookup_names(&my binding, names, errs);
    flow->contextual_depth += my binding.has_explicit_type;
    expr_lookup_names_initializer(&my value, names, errs);
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
        if (contextual->flags & TYF_ANY) contextual = nullptr;
    } else if ((my binding.tag == BINDING_OBJECT && vec_object_binding_item_size(&my binding.object)) ||
        (my binding.tag == BINDING_ARRAY && vec_binding_size(&my binding.array))
    ) {
        // Else what a pattern implies (tsc's getContextualTypeForInitializerExpression): `[1, "a"]` is a tuple there
        contextual = ty_binding_pattern_type(checker, &my binding, true);
    }
    return ty_check_expr(checker, &my value, contextual);
}

// `let x;` and `let x = null` under noImplicitAny: the type comes from what is assigned, as control flow sees it
// (tsc's autoType in getWidenedTypeForVariableLikeDeclaration)
static bool is_auto_typed(InitializedBinding const* me) {
    if (my binding.has_explicit_type || my binding.tag != BINDING_IDENTIFIER || !compiler_options()->no_implicit_any) {
        return false;
    }
    Symbol const* symbol = my binding.symbol;
    if (!symbol || !(symbol->flags & SYMBOL_VARIABLE) || symbol->is_parameter || symbol->is_const || symbol->is_ambient ||
        symbol->exported || symbol->parent
    ) {
        return false;
    }
    if (my value.tag == EXPR_IMPLICIT_ANY) return !my getter;
    return my value.tag == EXPR_LITERAL && (my value.literal.tag == VALUE_NULL || my value.literal.tag == VALUE_UNDEFINED);
}

Ty const* initialized_binding_evaluate_assigned_type(InitializedBinding* me) {
    Checker* checker = current_checker;
    if (my value.tag == EXPR_IMPLICIT_ANY && !my binding.has_explicit_type && my getter) {
        return ty_function_return_type(checker, my getter);
    }
    if (!my binding.has_explicit_type) {
        // A parameter of a function expression gets its type from the contextual signature, initializer or not
        if (my jsdoc_typed_by) ty_function_apply_jsdoc_type(checker, my jsdoc_typed_by);
        Ty const* contextual = ty_contextual_parameter_type(checker, me);
        if (contextual) return contextual;
        if (my is_rest_parameter && my value.tag == EXPR_IMPLICIT_ANY && my binding.tag == BINDING_IDENTIFIER) {
            return ty_array(checker, ty_intrinsic(checker, TY_ANY), false);
        }
        // An unannotated pattern without a default takes what the pattern implies (tsc's getWidenedTypeForVariableLikeDeclaration),
        // a rest parameter's too (`...{ a, b }`, `...[x, y]`)
        if (my value.tag == EXPR_IMPLICIT_ANY && (my binding.tag == BINDING_OBJECT || my binding.tag == BINDING_ARRAY)) {
            return ty_binding_pattern_type(checker, &my binding, false);
        }
    }
    if (is_auto_typed(me)) return ty_intrinsic(checker, TY_AUTO);
    // A call of a single non-generic signature has its type without its arguments being checked (tsc's
    // checkDeclarationInitializer): `const r = f(x)` doesn't depend on x, which may depend on r through a loop
    Ty const* ret = ty_quick_expr_type(checker, &my value, false);
    if (!ret) ret = initialized_binding_check_value(me);
    if (my binding.tag == BINDING_OBJECT || my binding.tag == BINDING_ARRAY) ret = ty_pad_initializer_type(checker, ret, &my binding);
    // HACK: `let x = []` is an evolving array in tsc (autoArrayType, under noImplicitAny), which grows with what is
    // pushed. Not there yet: it's any[] like before `[]` was never[]
    Symbol const* symbol = my binding.tag == BINDING_IDENTIFIER ? my binding.symbol : nullptr;
    if (!my binding.has_explicit_type && my value.tag == EXPR_ARRAY && !vec_expr_size(&my value.array) &&
        compiler_options()->no_implicit_any && symbol && !symbol->is_parameter && !symbol->is_ambient &&
        (symbol->flags & SYMBOL_VARIABLE)
    ) {
        return ty_array(checker, ty_intrinsic(checker, TY_ANY), false);
    }
    if (!my binding.has_explicit_type) {
        ret = ty_widened_for_declaration(checker, ret, my binding.tag == BINDING_IDENTIFIER && my abstract_literals);
        // A unique symbol made for another declaration is only a symbol here (tsc's widenTypeForVariableLikeDeclaration)
        if ((ret->flags & TYF_UNIQUE_ES_SYMBOL) && ret->literal.unique->key != symbol) {
            ret = ty_intrinsic(checker, TY_ES_SYMBOL);
        }
    }
    return ret;
}

void initialized_binding_typecheck(InitializedBinding* me, ErrorCtx* errs) {
    // `const x = require("m")`, `const x = require("m").a`: an alias, which is all tsc checks
    // (checkVariableLikeDeclaration: checkAliasSymbol and return)
    if (my require_port_count) return;
    initialized_binding_check_value(me);
    expr_typecheck(&my value, errs);
    binding_typecheck(&my binding, my value.tag == EXPR_IMPLICIT_ANY ? nullptr : &my value, errs);
}
