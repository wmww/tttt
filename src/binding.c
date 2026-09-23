#include "binding.h"
#include "flow.h"
#include "scope.h"
#include "several_types.h"
#include "display.h"
#include "error.h"
#include "ty/ty.h"

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
                props.full = i->target.rest ? i->target.element_span : i->target.content_span;
                i->target.parent = me;
                i->target.parent_item = i;
                binding_add_names(&i->target, nullptr, scope, props);
            }
            break;
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
        expr_lookup_names_initializer(my default_value, names, errs);
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

void binding_add_implicit_any_type_errors(Binding const* me, bool toplevel, ErrorCtx* errs) {
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

void binding_typecheck(Binding* me, ErrorCtx* errs) {
    if (my has_explicit_type) {
        type_typecheck(&my explicit_type, errs);
        ty_check_assignable(
            current_checker, ty_binding_assigned_type(current_checker, me), ty_of_node(&my explicit_type), my content_span, errs
        );
    }

    if (my default_value) {
        expr_typecheck(my default_value, errs);
    }

    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            if (my symbol) {
                Ty const* type = ty_binding_type(current_checker, me, (LowerCtx){ .generic = true });
                if ((type->flags & TYF_ERROR) && type == ty_intrinsic(current_checker, TY_ERROR) &&
                    compiler_options()->no_implicit_any && !my has_explicit_type
                ) {
                    error_ctx_error_d(errs, my content_span, 7022,
                        "'", my content_span, "' implicitly has type 'any' because it does not have a type annotation and is referenced directly or indirectly in "
                        "its own initializer.");
                }
            }
            break;

        case BINDING_ARRAY:
            Ty const* type = ty_binding_type(current_checker, me, (LowerCtx){ .generic = true });
            ty_iterated_type(current_checker, ITERATION_DESTRUCTURING, type, my content_span, errs);
            for_each (i, vec_binding, &my array) {
                binding_typecheck(i, errs);
            }
            break;

        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                if (i->computed_key) {
                    expr_typecheck(i->computed_key, errs);
                }
                binding_typecheck(&i->target, errs);
            }
            break;
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
    binding_drop(&my binding);
    expr_drop(&my value);
    lazy_type_drop(&my lazy_type);
}

void initialized_binding_add_names(InitializedBinding* me, Scope* scope, BindingAddNamesProps props) {
    lazy_type_bind(&my lazy_type, LAZY_TYPE_INITIALIZED_BINDING);
    my abstract_literals = props.abstract_literals;
    binding_add_names(&my binding, &my lazy_type, scope, props);
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
    binding_lookup_names(&my binding, names, errs);
    expr_lookup_names(&my value, names, errs);
    if (my value.tag != EXPR_IMPLICIT_ANY) binding_flow_assign(&my binding, names);
}

void initialized_binding_lookup_names_parameter(InitializedBinding* me, Scope* names, ErrorCtx* errs) {
    binding_lookup_names(&my binding, names, errs);
    expr_lookup_names_initializer(&my value, names, errs);
}

// The initializer, checked with the type annotation (if any) as its contextual type
static Ty const* initialized_binding_check_value(InitializedBinding* me) {
    Checker* checker = current_checker;
    Ty const* contextual = nullptr;
    if (my binding.has_explicit_type) {
        contextual = ty_from_node(checker, &my binding.explicit_type, (LowerCtx){ .generic = true });
        if (contextual->flags & TYF_ANY) contextual = nullptr;
    }
    return ty_check_expr(checker, &my value, contextual);
}

Ty const* initialized_binding_evaluate_assigned_type(InitializedBinding* me) {
    Checker* checker = current_checker;
    if (my value.tag == EXPR_IMPLICIT_ANY && !my binding.has_explicit_type && my getter) {
        return ty_function_return_type(checker, my getter);
    }
    if (!my binding.has_explicit_type) {
        // A parameter of a function expression gets its type from the contextual signature, initializer or not
        Ty const* contextual = ty_contextual_parameter_type(checker, me);
        if (contextual) return contextual;
    }
    // A call of a single non-generic signature has its type without its arguments being checked (tsc's
    // checkDeclarationInitializer): `const r = f(x)` doesn't depend on x, which may depend on r through a loop
    Ty const* ret = ty_quick_expr_type(checker, &my value, false);
    if (!ret) ret = initialized_binding_check_value(me);
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
    initialized_binding_check_value(me);
    expr_typecheck(&my value, errs);
    binding_typecheck(&my binding, errs);
}
