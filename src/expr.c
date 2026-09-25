#include "expr.h"
#include "source_file.h"
#include "stc/types.h"
#include "port_statement.h"
#include "type.h"
#include "error.h"
#include "scope.h"
#include "function_impl.h"
#include "generic.h"
#include "class_decl.h"
#include "interface_member.h"
#include "several_types.h"
#include "callable_type.h"
#include "flow.h"
#include "statement.h"
#include "ty/ty.h"
#include "unused.h"

const Expr expr_implicit_any_no_span = { .tag = EXPR_IMPLICIT_ANY };
const int as_type_precedence = 90;
const int ternary_precedence = 20;
const int new_keyword_precedence = 170;

void identifier_expr_drop(IdentifierExpr* me) {
    ignore me;
}

// tsc's checkThisBeforeSuper: directly in the constructor of a derived class (not `extends null`), `this` and
// `super.x` need a `super()` call on every way there. The flow graph behind the current node is complete already
static bool check_before_super(IdentifierExpr const* me, Scope const* names, unsigned code, char const* what, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    if (!flow || flow->dead || !flow->constructor_of || !names->function || !names->function->is_constructor) return true;
    ClassDecl const* class_decl = symbol_find_node(flow->constructor_of, SYMBOL_DECL_CLASS);
    if (!class_decl || class_decl->extends.tag == EXPR_IMPLICIT_ANY) return true;
    if (class_decl->extends.tag == EXPR_LITERAL && class_decl->extends.literal.tag == VALUE_NULL) return true;
    if (flow_is_post_super(flow->current)) return true;
    error_ctx_error_d(errs, my name, code, "'super' must be called before accessing ", what,
        " in the constructor of a derived class.");
    return false;
}

// tsc's checkSuperExpression up to its type: the member `super` is in (getSuperContainer: through arrow functions,
// except for `super(...)`) decides whether it may be used there (isLegalUsageOfSuperExpression). A legal `super` of a
// derived class targets that class, anything else is an error and any (an object literal's `super` is any too)
static void resolve_super(IdentifierExpr* me, Scope const* names, ErrorCtx* errs) {
    bool is_call = my super_call;
    Scope const* scope = names;
    bool legal = false;
    // A computed member name or a member decorator is outside the member, and so outside its class
    bool in_computed_name = false, skip_class = false;
    for (; scope; scope = scope->parent) {
        if (scope->no_super == NO_SUPER_LITERAL_COMPUTED_NAME) {
            in_computed_name = true;
            continue;
        }
        if (scope->no_super == NO_SUPER_COMPUTED_NAME || scope->no_super == NO_SUPER_DECORATOR) {
            in_computed_name |= scope->no_super == NO_SUPER_COMPUTED_NAME;
            skip_class = true;
            continue;
        }
        if (skip_class) {
            skip_class = scope->this_ref.kind != THIS_CLASS_INSTANCE && scope->this_ref.kind != THIS_CLASS_STATIC;
            continue;
        }
        if (scope->kind == SCOPE_FUNCTION && scope->function) {
            FunctionImpl const* function = scope->function;
            if (span_valid(function->arrow_span) && !is_call) continue;
            bool member = span_valid(function->member_name) || function->accessor;
            if (is_call) {
                legal = function->is_constructor;
            } else if (member && function->is_expression) {
                return; // An object literal's method: any
            } else {
                legal = function->is_constructor || member;
            }
            break;
        }
        // A static block, or a property initializer: the class's own scope
        if (scope->kind == SCOPE_STATIC_BLOCK || scope->this_ref.kind == THIS_CLASS_INSTANCE ||
            scope->this_ref.kind == THIS_CLASS_STATIC
        ) {
            legal = !is_call;
            break;
        }
    }
    if (!legal) {
        if (in_computed_name) {
            error_ctx_error_d(errs, my name, 2466, "'super' cannot be referenced in a computed property name.");
        } else if (is_call) {
            error_ctx_error_d(errs, my name, 2337,
                "Super calls are not permitted outside constructors or in nested functions inside constructors.");
        } else {
            error_ctx_error_d(errs, my name, 2660,
                "'super' can only be referenced in members of derived classes or object literal expressions.");
        }
        return;
    }
    bool in_constructor_args = scope->no_super == NO_SUPER_CONSTRUCTOR_ARGS;
    while (scope && scope->this_ref.kind != THIS_CLASS_INSTANCE && scope->this_ref.kind != THIS_CLASS_STATIC) {
        scope = scope->parent;
    }
    ClassDecl const* class_decl = scope ? scope->this_ref.class_decl : nullptr;
    if (!class_decl) return;
    if (class_decl->extends.tag == EXPR_IMPLICIT_ANY) {
        error_ctx_error_d(errs, my name, 2335, "'super' can only be referenced in a derived class.");
        return;
    }
    if (!is_call) check_before_super(me, names, 17011, "a property of 'super'", errs);
    if (in_constructor_args) {
        error_ctx_error_d(errs, my name, 2336, "'super' cannot be referenced in constructor arguments.");
        return;
    }
    my this_kind = scope->this_ref.kind;
    my target = scope->this_ref.symbol;
}

// tsc's checkResolvedBlockScopedVariable / isBlockScopedNameDeclaredBeforeUse: a `let`/`const`, class or enum used
// (as a value) before its declaration, unless the use is deferred: in a function, in an instance member
static void check_declared_before_use(IdentifierExpr const* me, Scope const* names, ErrorCtx* errs) {
    Symbol const* symbol = my symbol;
    u32 flags = symbol->flags;
    if (!(flags & (SYMBOL_BLOCK_SCOPED_VARIABLE | SYMBOL_CLASS | SYMBOL_ENUM)) || symbol->is_ambient) return;
    if (flags & SYMBOL_CLASS && flags & (SYMBOL_FUNCTION | SYMBOL_FUNCTION_SCOPED_VARIABLE)) return;
    if ((flags & SYMBOL_ENUM) == SYMBOL_CONST_ENUM && !compiler_options()->isolated_modules) return;
    if ((names->flow && names->flow->in_type_query) || names->ambient) return;
    SymbolDecl const* decl = nullptr;
    for (u32 i = 0; i < symbol->decl_count && !decl; i++) {
        SymbolDeclKind kind = symbol->decls[i].kind;
        if ((kind == SYMBOL_DECL_VARIABLE && symbol->decls[i].flags & SYMBOL_BLOCK_SCOPED_VARIABLE) ||
            kind == SYMBOL_DECL_CLASS || kind == SYMBOL_DECL_ENUM
        ) {
            decl = &symbol->decls[i];
        }
    }
    if (!decl || decl->full.source != my name.source) return;
    if (span_start_ptr(decl->full) <= span_start_ptr(my name)) {
        if (decl->kind == SYMBOL_DECL_CLASS) {
            // Declared before, but used in a computed name of its own members (`class A { [A.p]() {} }`)
            bool in_computed_name = false;
            Scope const* i = names;
            for (; i && i->kind != SCOPE_FUNCTION && !i->this_ref.class_decl; i = i->parent) {
                in_computed_name |= i->no_super == NO_SUPER_COMPUTED_NAME;
            }
            if (!in_computed_name || !i || i->this_ref.class_decl != decl->node) return;
        } else {
            // Declared before, but used in its own initializer (`let x = x`, `for (const x of x)`)
            if (decl->kind != SYMBOL_DECL_VARIABLE || !names->flow || !names->flow->initializing) return;
            Binding const* binding = decl->node;
            Binding const* root = binding;
            while (root->parent) root = root->parent;
            if (root != &names->flow->initializing->binding) return;
            // In the default of another element of the pattern it's initialized already (`{ a, b = a }`)
            char const* at = span_start_ptr(my name);
            bool in_pattern = at >= span_start_ptr(root->content_span) && at < span_end_ptr(root->content_span);
            Span element = span_valid(binding->element_span) ? binding->element_span : binding->content_span;
            if (binding->default_value) element = span_between(element, binding->default_value->outer_span);
            if (in_pattern && !(at >= span_start_ptr(element) && at < span_end_ptr(element))) return;
        }
    } else {
        // Used first: fine if the use is deferred until after the declaration ran
        bool through_computed_name = false;
        for (Scope const* i = names; i && !scope_declares_symbol(i, symbol); i = i->parent) {
            if (i->kind == SCOPE_FUNCTION) return;
            // A computed member name runs with the class, the members don't
            if (i->no_super == NO_SUPER_COMPUTED_NAME) through_computed_name = true;
            if (i->this_ref.kind == THIS_CLASS_INSTANCE && !through_computed_name) return;
            if (i->this_ref.class_decl) through_computed_name = false;
        }
    }
    Error* error;
    if (flags & SYMBOL_BLOCK_SCOPED_VARIABLE) {
        error = error_ctx_error_d(errs, my name, 2448, "Block-scoped variable '", decl->name, "' used before its declaration.");
    } else if (flags & SYMBOL_CLASS) {
        error = error_ctx_error_d(errs, my name, 2449, "Class '", decl->name, "' used before its declaration.");
    } else {
        error = error_ctx_error_d(errs, my name, 2450, "Enum '", decl->name, "' used before its declaration.");
    }
    error_add_related_d(error, decl->name, 2728, "'", decl->name, "' is declared here.");
}

// tsc's onSuccessfullyResolvedSymbol for parameter initializers: a parameter's initializer (or its pattern's defaults
// and computed names) runs before the parameters after it are, and before it is itself
static void check_parameter_reference(IdentifierExpr const* me, Scope const* names, ErrorCtx* errs) {
    FlowBuilder const* flow = names->flow;
    Binding const* param = flow ? flow->parameter_initializing : nullptr;
    if (!param || flow->in_type_query) return;
    Symbol const* symbol = my symbol;
    Span param_name = param->content_span;
    if (param->tag == BINDING_IDENTIFIER && symbol == param->symbol) {
        error_ctx_error_d(errs, my name, 2372, "Parameter '", param_name, "' cannot reference itself.");
    } else if (span_start_ptr(symbol->name_span) > span_start_ptr(param_name) && symbol->name_span.source == param_name.source &&
        flow->parameter_function_scope && scope_declares_symbol(flow->parameter_function_scope, symbol)
    ) {
        error_ctx_error_d(errs, my name, 2373,
            "Parameter '", param_name, "' cannot reference identifier '", my name, "' declared after it.");
    }
}

// Whether a function is around that has `arguments`: from ES2015 on an arrow function sees the arguments of the
// function it is in, before that its own (which is TS2496)
static bool scope_has_arguments(Scope const* scope) {
    bool arrows = compiler_options()->target < SCRIPT_TARGET_ES2015;
    for (; scope; scope = scope->parent) {
        if (scope->kind == SCOPE_FUNCTION && scope->function && (arrows || !span_valid(scope->function->arrow_span))) {
            return true;
        }
    }
    return false;
}

// TS1361/TS1362: a value that was imported or exported as a type only (tsc's
// checkAndReportErrorForResolvingImportAliasToTypeOnlySymbol). Fine in `typeof x` types and ambient contexts
static void check_type_only_value(IdentifierExpr const* me, Scope const* names, ErrorCtx* errs) {
    Symbol const* found = my symbol;
    if (!found || !(found->flags & SYMBOL_ALIAS) || found->flags & SYMBOL_VALUE) return;
    if (names->flow && (names->flow->in_type_query || names->flow->type_only_alias_ok)) return;
    for (Scope const* scope = names; scope; scope = scope->parent) {
        if (scope->ambient) return;
    }
    PortItem const* item = symbol_type_only_declaration(found);
    // (what the type-only declaration names has to be a value: a merge further down the chain may be what is)
    if (!item || !item->symbol || !(symbol_resolved_flags(item->symbol) & SYMBOL_VALUE)) return;
    bool exported = port_item_is_export(item);
    Error* error = error_ctx_error_d(errs, my name, exported ? 1362 : 1361, "'", my name, "' cannot be used as a value "
        "because it was ", exported ? "exported using 'export type'." : "imported using 'import type'.");
    if (error) {
        error_add_related_d(error, item->decl_name, exported ? 1377 : 1376, "'", my name, "' was ",
            exported ? "exported" : "imported", " here.");
    }
}

void identifier_expr_lookup_names(IdentifierExpr* me, Scope* names, ErrorCtx* errs) {
    if (my keyword) {
        if (my keyword == KEYWORD_IDENTIFIER_THIS) {
            ThisRef this_ref = scope_get_this(names, false);
            my this_kind = this_ref.kind;
            my target = this_ref.symbol;
            my this_function = this_ref.function;
            check_before_super(me, names, 17009, "'this'", errs);
            if (!this_ref.kind) {
                // At the top level: of a script `typeof globalThis`, of a module `undefined`; in a namespace an error
                Scope const* file = names;
                while (file->parent && file->kind != SCOPE_MODULE && file->kind != SCOPE_SCRIPT && file->kind != SCOPE_NAMESPACE &&
                    file->kind != SCOPE_ENUM
                ) {
                    file = file->parent;
                }
                if (file->kind == SCOPE_SCRIPT) my this_kind = THIS_GLOBAL;
                if (file->kind == SCOPE_NAMESPACE) my this_kind = THIS_NAMESPACE;
                if (file->kind == SCOPE_ENUM) my this_kind = THIS_ENUM;
            }
        } else if (my keyword == KEYWORD_IDENTIFIER_SUPER) {
            resolve_super(me, names, errs);
        } else if (my keyword == KEYWORD_IDENTIFIER_NEW_TARGET) {
            // tsc's getNewTargetContainer(): the nearest function that isn't an arrow function, which must be a
            // constructor (the class), a function declaration or a function expression
            Scope const* scope = names;
            while (scope && scope->kind != SCOPE_STATIC_BLOCK &&
                !(scope->kind == SCOPE_FUNCTION && scope->function && !span_valid(scope->function->arrow_span))
            ) {
                scope = scope->parent;
            }
            FunctionImpl const* function = scope && scope->kind == SCOPE_FUNCTION ? scope->function : nullptr;
            if (function && function->is_constructor) {
                my target = function->constructed_class;
            } else if (function && !function->is_method && !function->accessor && !span_valid(function->member_name)) {
                my target = function->symbol;
                my this_function = function;
            } else {
                error_ctx_error_d(errs, my name, 17013, "Meta-property 'new.target' is only allowed in the body of a "
                    "function declaration, function expression, or constructor.");
            }
        } else if (my keyword == KEYWORD_IDENTIFIER_IMPORT && my module_ref) {
            // `import("./a")`
            if (my module_ref->file) {
                module_ref_report_resolved(my module_ref, errs);
            } else if (!binder_module_of_ref(names->binder, my module_ref)) {
                module_ref_report_unresolved(my module_ref, errs);
            }
        }
        return;
    }
    my target = scope_get_value(names, my name.sv, &my symbol);
    if (my target) {
        check_type_only_value(me, names, errs);
        check_declared_before_use(me, names, errs);
        check_parameter_reference(me, names, errs);
        my outer_reference = my target->flow_start && names->flow && names->flow->start != my target->flow_start;
    } else if (span_equals(my name, "arguments") && scope_has_arguments(names)) {
        // No symbol: ty_identifier_reference_type() makes it IArguments
        my is_arguments = true;
        FunctionImpl const* container = names->function;
        if (compiler_options()->target < SCRIPT_TARGET_ES2015 && container) {
            if (span_valid(container->arrow_span)) {
                error_ctx_error_d(errs, my name, 2496, "The 'arguments' object cannot be referenced in an arrow function in ES5. "
                    "Consider using a standard function expression.");
            } else if (container->is_async) {
                error_ctx_error_d(errs, my name, 2522, "The 'arguments' object cannot be referenced in an async function or "
                    "method in ES5. Consider using a standard function or method.");
            }
        }
    } else if (scope_get_type(names, my name.sv, nullptr)) {
        // (tsc's isES2015OrLaterConstructorName)
        static char const* const later[] = { "Promise", "Symbol", "Map", "WeakMap", "Set", "WeakSet" };
        bool later_lib = false;
        for (u32 i = 0; i < c_arraylen(later); i++) later_lib |= csview_equals(my name.sv, later[i]);
        if (later_lib) {
            error_ctx_error_d(errs, my name, 2585, "'", my name, "' only refers to a type, but is being used as a value "
                "here. Do you need to change your target library? Try changing the 'lib' compiler option to es2015 or later.");
        } else {
            error_ctx_error_d(errs, my name, 2693, "'", my name, "' only refers to a type, but is being used as a value here.");
        }
    } else {
        scope_report_value_not_found(names, my name, my shorthand, errs);
    }
}

Ty const* identifier_expr_evaluate_type(IdentifierExpr const* me) {
    return ty_identifier_reference_type(current_checker, me);
}

// tsc's closeEnoughKind: a type that has a good chance of being a valid operand of an arithmetic operator
static bool plausible_operand(Ty const* ty) {
    u32 close_enough = TYF_NUMBER | TYF_BIGINT | TYF_STRING | TYF_ANY | TYF_UNKNOWN | TYF_NUMBER_LITERAL |
        TYF_BIGINT_LITERAL | TYF_STRING_LITERAL | TYF_ENUM;
    if (!compiler_options()->strict_null_checks) close_enough |= TYF_NULLABLE; // Assignable to anything
    if (ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!plausible_operand(ty->composite.types.data[i])) return false;
        }
        return true;
    }
    return (ty->flags & close_enough) != 0;
}

// tsc's checkIdentifier on an assignment target: only a variable can be assigned, and not a constant
static bool report_invalid_assignment_target(IdentifierExpr const* me, ErrorCtx* errs) {
    Symbol const* symbol = my symbol;
    if (!my assignment_target || !symbol || !my target || symbol->is_placeholder || my keyword) return false;
    if (!(symbol->flags & SYMBOL_VARIABLE) && !(my name.source && my name.source->is_js && (symbol->flags & SYMBOL_VALUE_MODULE))) {
        char const* what = symbol->flags & SYMBOL_ENUM ? "an enum" : symbol->flags & SYMBOL_CLASS ? "a class" :
            symbol->flags & (SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE) ? "a namespace" :
            symbol->flags & SYMBOL_FUNCTION ? "a function" : symbol->flags & SYMBOL_ALIAS ? "an import" : nullptr;
        unsigned code = symbol->flags & SYMBOL_ENUM ? 2628 : symbol->flags & SYMBOL_CLASS ? 2629 :
            symbol->flags & (SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE) ? 2631 : symbol->flags & SYMBOL_FUNCTION ? 2630 :
            symbol->flags & SYMBOL_ALIAS ? 2632 : 2539;
        if (what) {
            error_ctx_error_d(errs, my name, code, "Cannot assign to '", my name, "' because it is ", what, ".");
        } else {
            error_ctx_error_d(errs, my name, code, "Cannot assign to '", my name, "' because it is not a variable.");
        }
        return true;
    }
    if (symbol->is_const) {
        error_ctx_error_d(errs, my name, 2588, "Cannot assign to '", my name, "' because it is a constant.");
        return true;
    }
    return false;
}

bool identifier_is_invalid_assignment_target(IdentifierExpr const* me) {
    Symbol const* symbol = my symbol;
    if (!my assignment_target || !symbol || !my target || symbol->is_placeholder || my keyword) return false;
    return !(symbol->flags & SYMBOL_VARIABLE) || symbol->is_const;
}

void identifier_expr_typecheck(IdentifierExpr const* me, ErrorCtx* errs) {
    report_invalid_assignment_target(me, errs);
    switch (ty_identifier_flow_issue(current_checker, me)) {
        case IDENTIFIER_FLOW_OK: break;
        case IDENTIFIER_FLOW_UNASSIGNED:
            error_ctx_error_d(errs, my name, 2454, "Variable '", my name, "' is used before being assigned.");
            break;
        case IDENTIFIER_FLOW_IMPLICIT_ANY: {
            Binding const* binding = symbol_find_node(my target, SYMBOL_DECL_VARIABLE);
            error_ctx_error_d(errs, binding->content_span, 7034,
                "Variable '", my name, "' implicitly has type 'any' in some locations where its type cannot be determined.");
            error_ctx_error_d(errs, my name, 7005, "Variable '", my name, "' implicitly has an 'any' type.");
        }   break;
    }
    if (my keyword == KEYWORD_IDENTIFIER_THIS && my this_kind == THIS_NAMESPACE) {
        error_ctx_error_d(errs, my name, 2331, "'this' cannot be referenced in a module or namespace body.");
    }
    if (my keyword == KEYWORD_IDENTIFIER_THIS && my this_kind == THIS_ENUM) {
        error_ctx_error_d(errs, my name, 2332, "'this' cannot be referenced in current location.");
    }
    // `this` in a function nothing says the `this` of (tsc's checkThisExpression)
    if (my keyword == KEYWORD_IDENTIFIER_THIS && my this_kind == THIS_ANY && compiler_options()->no_implicit_this &&
        !(my this_function && ty_contextual_this_type(current_checker, my this_function))
    ) {
        error_ctx_error_d(errs, my name, 2683, "'this' implicitly has type 'any' because it does not have a type annotation.");
    }
}

// `f<A, B>` outside of a call (there the type arguments are the call's business, ty/call.c)
void generic_params_expr_typecheck(GenericParamsExpr* me, ErrorCtx* errs) {
    for_each (i, vec_spanned_type, &my params) {
        type_typecheck(&i->type, errs);
    }
    Checker* checker = current_checker;
    Ty const* callee = ty_expr_type(checker, &my expr);
    if (callee->flags & TYF_ANY) return;
    // TODO: every signature that takes this many type arguments, like ty_apply_type_arguments()
    Members const* members = ty_members(checker, callee);
    Signature const* sig = members->call_count ? members->call[0] : members->construct_count ? members->construct[0] : nullptr;
    if (sig) signature_resolve(checker, sig);
    if (!sig || !sig->type_params.len) {
        error_ctx_error_d(
            errs,
            span_between(
                vec_spanned_type_at(&my params, 0)->span,
                vec_spanned_type_back(&my params)->span
            ),
            2558,
            "Expected 0 type arguments, but got ", vec_spanned_type_size(&my params), "."
        );
        return;
    }
    for (u32 i = 0; i < sig->type_params.len && (isize_t)i < vec_spanned_type_size(&my params); i++) {
        Ty const* constraint = ty_param_constraint(checker, sig->type_params.data[i]);
        if (!constraint || ty_type_argument_satisfies_constraint(checker, sig->type_params, &my params, i)) continue;
        SpannedType const* arg = vec_spanned_type_at(&my params, i);
        error_ctx_error_d(errs, arg->span, 2344,
            "Type '", ty_base_type_of_literal(checker, ty_of_node(&arg->type)), "' does not satisfy the constraint '", constraint, "'.");
    }
}

void expr_drop(Expr* me) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
            break;

        case EXPR_LITERAL:
            value_drop(&my literal);
            break;

        case EXPR_IDENTIFIER:
            identifier_expr_drop(&my identifier);
            break;

        case EXPR_BINARY_OP:
            expr_drop(&my binary_op->left);
            expr_drop(&my binary_op->right);
            free(my binary_op);
            break;

        case EXPR_UNARY_OP:
            expr_drop(&my unary_op->expr);
            free(my unary_op);
            break;

        case EXPR_PROPERTY:
            expr_drop(&my property->expr);
            expr_drop(&my property->prop);
            free(my property);
            break;

        case EXPR_GENERIC_PARAMS:
            expr_drop(&my generic_params->expr);
            vec_spanned_type_drop(&my generic_params->params);
            free(my generic_params);
            break;

        case EXPR_AS_TYPE:
            expr_drop(&my as_type->expr);
            type_drop(&my as_type->type);
            free(my as_type);
            break;

        case EXPR_TERNARY:
            expr_drop(&my ternary->condition);
            expr_drop(&my ternary->yes);
            expr_drop(&my ternary->no);
            free(my ternary);
            break;

        case EXPR_CALL:
            expr_drop(&my call->callable);
            vec_expr_drop(&my call->args);
            free(my call);
            break;

        case EXPR_FUNCTION:
            function_impl_drop(my function);
            free(my function);
            break;

        case EXPR_OBJECT:
            vec_object_expr_item_drop(&my object);
            break;

        case EXPR_ARRAY:
            vec_expr_drop(&my array);
            break;

        case EXPR_CLASS_DECL:
            class_decl_drop(my class_decl);
            free(my class_decl);
            break;

        case EXPR_SPREAD:
            expr_drop(my spread);
            free(my spread);
            break;

        case EXPR_REGEX:
            break;

        case EXPR_PRIVATE_NAME:
            free(my private_name);
            break;

        case EXPR_TEMPLATE:
            if (my template_->tag) {
                expr_drop(my template_->tag);
                free(my template_->tag);
            }
            vec_expr_drop(&my template_->substitutions);
            cstr_drop(&my template_->plain_string);
            vec_cstr_drop(&my template_->texts);
            free(my template_);
            break;

        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT:
            jsx_element_drop(my jsx);
            free(my jsx);
            break;

        case EXPR_JSX_ATTRIBUTES:
            vec_jsx_attribute_drop(&my jsx_attributes);
            break;
    }
    lazy_type_drop(&my lazy_type);
}

void jsx_attribute_drop(JsxAttribute* me) {
    expr_drop(&my value);
}

void jsx_child_drop(JsxChild* me) {
    expr_drop(&my expr);
}

void jsx_element_drop(JsxElement* me) {
    expr_drop(&my tag_name);
    vec_spanned_type_drop(&my type_args);
    expr_drop(&my attributes);
    vec_jsx_child_drop(&my children);
    expr_drop(&my closing_name);
}

bool jsx_element_is_intrinsic(JsxElement const* me) {
    if (my namespaced) return true;
    if (my tag_name.tag != EXPR_IDENTIFIER || my tag_name.identifier.keyword) return false;
    csview name = my tag_name.identifier.name.sv;
    u32 first = name.size ? (uint8_t)name.buf[0] : 0;
    if (first == '\\' && name.size > 2) {
        // `\u0061` / `\u{61}` (TS17021, but the name is still `a`)
        first = 0;
        for (isize i = name.buf[2] == '{' ? 3 : 2; i < name.size && isxdigit((uint8_t)name.buf[i]); i++) {
            first = first * 16 + (u32)(isdigit((uint8_t)name.buf[i]) ? name.buf[i] - '0' : (tolower((uint8_t)name.buf[i]) - 'a' + 10));
            if (name.buf[2] != '{' && i == 5) break;
        }
    }
    return (first >= 'a' && first <= 'z') || csview_contains(name, "-");
}

// `x as const` / `<const>x`: `const` isn't a type name (the new core checks the same, see is_const_assertion())
static bool as_type_is_const_assertion(AsTypeExpr const* me) {
    return my type.tag == TYPE_IDENTIFIER && span_equals(my type.identifier->name, "const") &&
        vec_span_is_empty(&my type.identifier->namespaces) && !vec_spanned_type_size(&my type.identifier->generic_params);
}

// Control flow (flow.h). These follow the functions of the same names in tsc's binder

static bool binary_op_is_logical(BinaryOp op) {
    return op == BINARY_OP_AND_AND || op == BINARY_OP_OR_OR || op == BINARY_OP_EH_EH;
}

static bool binary_op_is_logical_assignment(BinaryOp op) {
    return op == BINARY_OP_AND_AND_EQUAL || op == BINARY_OP_OR_OR_EQUAL || op == BINARY_OP_EH_EH_EQUAL;
}

static bool binary_op_is_assignment(BinaryOp op) {
    return op >= BINARY_OP_EQUAL && op <= BINARY_OP_EH_EH_EQUAL;
}

// `a && b`, `!(a || b)`: binds its own condition nodes
static bool expr_is_logical(Expr const* me) {
    while (my tag == EXPR_UNARY_OP && my unary_op->op == UNARY_OP_LEFT_BANG) me = &my unary_op->expr;
    return my tag == EXPR_BINARY_OP && binary_op_is_logical(my binary_op->op);
}

static void flow_add_conditions_flagged(
    FlowBuilder* flow, Expr const* me, FlowNode* true_target, FlowNode* false_target, u32 flags
) {
    flow_add_antecedent(flow, true_target, flow_new_condition(flow, FLOW_TRUE_CONDITION | flags, flow->current, me));
    flow_add_antecedent(flow, false_target, flow_new_condition(flow, FLOW_FALSE_CONDITION | flags, flow->current, me));
}

static void flow_add_conditions(FlowBuilder* flow, Expr const* me, FlowNode* true_target, FlowNode* false_target) {
    flow_add_conditions_flagged(flow, me, true_target, false_target, 0);
}

// `true ?? x`, `true?.x`: `true` decides nothing about where control goes
static void flow_add_nullish_conditions(FlowBuilder* flow, Expr const* me, FlowNode* true_target, FlowNode* false_target) {
    if (my tag == EXPR_LITERAL && my literal.tag == VALUE_BOOLEAN) {
        flow_add_antecedent(flow, true_target, flow->current);
        flow_add_antecedent(flow, false_target, flow->current);
    } else {
        flow_add_conditions_flagged(flow, me, true_target, false_target, FLOW_NULLISH);
    }
}

void expr_lookup_names_condition(Expr* me, Scope* names, ErrorCtx* errs, FlowNode* true_target, FlowNode* false_target) {
    FlowBuilder* flow = names->flow;
    bool nullish = flow->nullish_condition;
    flow->true_target = true_target;
    flow->false_target = false_target;
    expr_lookup_names(me, names, errs);
    bool binds_its_own = expr_is_logical(me) || expr_is_optional_chain(me) ||
        (my tag == EXPR_BINARY_OP && binary_op_is_logical_assignment(my binary_op->op));
    if (!binds_its_own) flow_add_conditions_flagged(flow, me, true_target, false_target, nullish ? FLOW_NULLISH : 0);
}

// A call with a dotted name as its callee may be an assertion or never return, the checker finds out
static void flow_maybe_call(FlowBuilder* flow, Expr const* me) {
    if (my tag != EXPR_CALL || my call->uses_new || flow->dead) return;
    Expr const* callee = &my call->callable;
    while (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER) return;
    if (expr_is_dotted_name(callee)) flow->current = flow_new_call(flow, flow->current, me);
}

void expr_lookup_names_statement(Expr* me, Scope* names, ErrorCtx* errs) {
    expr_lookup_names(me, names, errs);
    flow_maybe_call(names->flow, me);
}

static void flow_assignment_target(FlowBuilder* flow, Expr const* target, FlowAssign assign);

// An element of an assignment pattern, which may have a default: `[a = 1] = e`
static void flow_destructuring_target(FlowBuilder* flow, Expr const* target, FlowAssign assign, FlowPatternPath path) {
    if (target->tag == EXPR_BINARY_OP && target->binary_op->op == BINARY_OP_EQUAL) {
        path.default_value = &target->binary_op->right;
        target = &target->binary_op->left;
    }
    FlowPatternPath* node = arena_new(flow->arena, FlowPatternPath);
    *node = path;
    assign.kind = FLOW_ASSIGN_PATTERN;
    assign.path = node;
    flow_assignment_target(flow, target, assign);
}

static void flow_assignment_target(FlowBuilder* flow, Expr const* target, FlowAssign assign) {
    if (expr_is_narrowable_reference(target)) {
        assign.target = target;
        flow_assign(flow, assign);
    } else if (target->tag == EXPR_ARRAY) {
        for_each (i, vec_expr, &target->array) {
            FlowPatternPath path = { .parent = assign.path, .pattern = target, .index = (u32)i_index };
            if (i->tag == EXPR_SPREAD) {
                path.kind = FLOW_PATH_REST;
                flow_destructuring_target(flow, i->spread, assign, path);
            } else {
                path.kind = FLOW_PATH_ELEMENT;
                flow_destructuring_target(flow, i, assign, path);
            }
        }
    } else if (target->tag == EXPR_OBJECT) {
        for_each (i, vec_object_expr_item, &target->object) {
            FlowPatternPath path = { .parent = assign.path, .pattern = target, .item = i };
            switch (i->tag) {
                case OBJECT_ITEM_PROPERTY:
                case OBJECT_ITEM_SHORTHAND:
                    path.kind = FLOW_PATH_PROPERTY;
                    flow_destructuring_target(flow, &i->value, assign, path);
                    break;
                case OBJECT_ITEM_SPREAD:
                    path.kind = FLOW_PATH_REST;
                    flow_destructuring_target(flow, &i->value, assign, path);
                    break;
                default:
                    break;
            }
        }
    }
}

void expr_flow_assign_for_each(Expr const* me, Scope* names, ForEachLoopStatement const* for_each) {
    flow_assignment_target(names->flow, me, (FlowAssign){ .kind = FLOW_ASSIGN_FOR_EACH, .for_each = for_each });
}

// What narrowing inside closures needs to know about a local variable (tsc's markNodeAssignments())
static void identifier_mark_assigned(IdentifierExpr const* me, Scope const* names) {
    Symbol* symbol = (Symbol*)my target; // Local: this file's walk owns it
    if (!symbol || !symbol->is_local || !(symbol->flags & SYMBOL_VARIABLE)) return;
    symbol->is_assigned = true;
    if (my assignment_target == FLOW_TARGET_DEFINITE) symbol->is_definitely_assigned = true;
    char const* anywhere = (char const*)UINTPTR_MAX;
    if (symbol->last_assignment_end == anywhere) return;
    if (names->function != symbol->function) {
        symbol->is_assigned_in_closure = true;
        symbol->last_assignment_end = anywhere;
        return;
    }
    // The end of the outermost statement around the assignment that starts after the declaration
    char const* end = span_end_ptr(my name);
    char const* declared_at = span_start_ptr(symbol->name_span);
    for (FlowStatementFrame const* i = names->flow->statements; i; i = i->parent) {
        if (span_start_ptr(i->span) <= declared_at) break;
        end = span_end_ptr(i->span);
    }
    if (end > symbol->last_assignment_end) symbol->last_assignment_end = end;
}

// tsc's isCompoundLikeAssignment: what is assigned is a shift, additive, multiplicative or exponentiation expression
static bool is_compound_like(Expr const* right) {
    if (right->tag != EXPR_BINARY_OP) return false;
    BinaryOp op = right->binary_op->op;
    return op >= BINARY_OP_STAR_STAR && op <= BINARY_OP_GREATER_GREATER_GREATER;
}

static void expr_lookup_names_logical(Expr* me, Scope* names, ErrorCtx* errs, FlowNode* true_target, FlowNode* false_target) {
    FlowBuilder* flow = names->flow;
    BinaryOp op = my binary_op->op;
    FlowNode* pre_right = flow_new_branch_label(flow);
    bool is_assignment = binary_op_is_logical_assignment(op);
    if (is_assignment) {
        flow->assignment_target = FLOW_TARGET_DEFINITE;
        flow->read_target = true;
    }
    if (op == BINARY_OP_AND_AND || op == BINARY_OP_AND_AND_EQUAL) {
        expr_lookup_names_condition(&my binary_op->left, names, errs, pre_right, false_target);
    } else if (op == BINARY_OP_EH_EH && my binary_op->left.tag == EXPR_LITERAL) {
        expr_lookup_names(&my binary_op->left, names, errs);
        flow_add_nullish_conditions(flow, &my binary_op->left, true_target, pre_right);
    } else {
        flow->nullish_condition = op == BINARY_OP_EH_EH || op == BINARY_OP_EH_EH_EQUAL;
        expr_lookup_names_condition(&my binary_op->left, names, errs, true_target, pre_right);
    }
    flow_set_current(flow, flow_finish_label(pre_right));
    if (is_assignment) {
        // The right side is a logical expression of its own (tsc's isTopLevelLogicalExpression)
        expr_lookup_names(&my binary_op->right, names, errs);
        flow_assignment_target(flow, &my binary_op->left, (FlowAssign){ .kind = FLOW_ASSIGN_EXPR, .assignment = me });
        flow_add_conditions(flow, me, true_target, false_target);
    } else if (op == BINARY_OP_EH_EH && my binary_op->right.tag == EXPR_LITERAL) {
        expr_lookup_names(&my binary_op->right, names, errs);
        flow_add_nullish_conditions(flow, &my binary_op->right, true_target, false_target);
    } else {
        expr_lookup_names_condition(&my binary_op->right, names, errs, true_target, false_target);
    }
}

// `a?.b` is `a && a.b` as far as flow goes. `continues`: something further out continues the chain
static void expr_lookup_names_chain(
    Expr* me, Scope* names, ErrorCtx* errs, FlowNode* true_target, FlowNode* false_target, bool continues, bool nullish
) {
    FlowBuilder* flow = names->flow;
    bool is_root = expr_is_optional_chain_root(me);
    FlowNode* pre_chain = is_root ? flow_new_branch_label(flow) : nullptr;
    Expr* object =
        my tag == EXPR_PROPERTY ? &my property->expr :
        my tag == EXPR_CALL ? &my call->callable : &my unary_op->expr;
    flow->true_target = pre_chain ? pre_chain : true_target;
    flow->false_target = false_target;
    flow->chain_continues = !is_root;
    expr_lookup_names(object, names, errs);
    if (is_root) {
        // (When the object is a chain of its own it has added the same already: so does tsc)
        flow_add_nullish_conditions(flow, object, pre_chain, false_target);
        flow_set_current(flow, flow_finish_label(pre_chain));
    }
    if (my tag == EXPR_PROPERTY) {
        expr_lookup_names(&my property->prop, names, errs);
    } else if (my tag == EXPR_CALL) {
        for_each (i, vec_expr, &my call->args) {
            expr_lookup_names(i, names, errs);
        }
    }
    if (!continues) flow_add_conditions_flagged(flow, me, true_target, false_target, nullish ? FLOW_NULLISH : 0);
}

static void expr_lookup_names_chain_flow(
    Expr* me, Scope* names, ErrorCtx* errs, FlowNode* true_target, FlowNode* false_target, bool continues, bool nullish
) {
    FlowBuilder* flow = names->flow;
    if (true_target) {
        expr_lookup_names_chain(me, names, errs, true_target, false_target, continues, nullish);
        return;
    }
    // Not a condition of anything
    FlowNode* post = flow_new_branch_label(flow);
    FlowNode* saved = flow->current;
    bool saved_effects = flow->has_flow_effects; // tsc doesn't reset it here
    expr_lookup_names_chain(me, names, errs, post, post, false, false);
    flow_set_current(flow, flow->has_flow_effects ? flow_finish_label(post) : saved);
    flow->has_flow_effects |= saved_effects;
}

// tsc's isIndirectCall: `(0, o.f)()` and `(0, eval)()` call without a `this`, the comma isn't pointless
static void mark_indirect_call(Expr* callee) {
    if (callee->tag != EXPR_BINARY_OP || !expr_is_perenthisized(callee) || callee->binary_op->op != BINARY_OP_COMMA) return;
    Expr const* left = &callee->binary_op->left;
    Expr const* right = &callee->binary_op->right;
    if (left->tag != EXPR_LITERAL || left->literal.tag != VALUE_NUMBER || expr_is_perenthisized(left) ||
        !csview_equals(left->inner_span.sv, "0")
    ) {
        return;
    }
    bool eval = right->tag == EXPR_IDENTIFIER && !right->identifier.keyword && csview_equals(right->identifier.name.sv, "eval");
    if (right->tag == EXPR_PROPERTY || eval) callee->binary_op->indirect_call = true;
}

// An entity name as written in a pragma or option: its first identifier (`React` of `React.createElement`)
static csview first_identifier(csview sv) {
    isize dot = csview_find(sv, ".");
    return dot == c_NPOS ? sv : csview_slice(sv, 0, dot);
}

// tsc's getJsxNamespace: the name the factory of an element (or fragment) starts with
static csview jsx_factory_name(SourceFile const* file, bool fragment) {
    CompilerOptions const* options = compiler_options();
    if (fragment) {
        if (file && file->jsx_pragmas.fragment_factory.size) return first_identifier(file->jsx_pragmas.fragment_factory);
        if (cstr_size(&options->jsx_fragment_factory)) return first_identifier(cstr_sv(&options->jsx_fragment_factory));
    } else if (file && file->jsx_pragmas.factory.size) {
        return first_identifier(file->jsx_pragmas.factory);
    }
    if (cstr_size(&options->jsx_factory)) return first_identifier(cstr_sv(&options->jsx_factory));
    if (cstr_size(&options->react_namespace)) return cstr_sv(&options->react_namespace);
    return c_sv("React");
}

// tsc's getJSXImplicitImportBase: whether elements import their factory from `<source>/jsx-runtime`
static bool jsx_runtime_import(SourceFile const* file) {
    CompilerOptions const* options = compiler_options();
    csview runtime = file ? file->jsx_pragmas.runtime : c_sv("");
    if (csview_equals(runtime, "classic")) return false;
    return options->jsx == JSX_REACT_JSX || options->jsx == JSX_REACT_JSXDEV || cstr_size(&options->jsx_import_source) ||
        (file && file->jsx_pragmas.import_source.size) || csview_equals(runtime, "automatic");
}

// tsc's markJsxAliasReferenced (and the namespace half of getJsxNamespaceAt): every element refers to its factory
static void jsx_lookup_factory(JsxElement* el, bool fragment, Scope* names, ErrorCtx* errs) {
    csview name = jsx_factory_name(names->file, fragment);
    el->factory_namespace = scope_resolve(names, name, SYMBOL_NAMESPACE);
    // TODO(plan 22 stage 4): the factory is referenced when the runtime module can't be found. For now it always is
    el->runtime_import = jsx_runtime_import(names->file);
    if (el->runtime_import) return;
    JsxKind jsx = compiler_options()->jsx;
    // `null` as the fragment factory (tsc allows it)
    if (fragment && csview_equals(name, "null")) return;
    u32 meaning = jsx == JSX_PRESERVE || jsx == JSX_REACT_NATIVE ? SYMBOL_VALUE & ~(u32)SYMBOL_ENUM : SYMBOL_VALUE;
    Symbol* found = scope_resolve(names, name, meaning);
    el->factory_value = found;
    if (found) {
        unused_mark_referenced(names, found, SYMBOL_VALUE);
    } else if (jsx == JSX_REACT) {
        Span at = fragment || !el->tag_name.inner_span.sv.size ? el->opening_span : el->tag_name.inner_span;
        error_ctx_error_d(errs, at, 2874, "This JSX tag requires '", name, "' to be in scope, but it could not be found.");
    }
}

// Not inlined, like jsx_typecheck()
__attribute__((noinline)) static void jsx_lookup_names(Expr* me, Scope* names, ErrorCtx* errs) {
    switch (my tag) {
        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT: {
            jsx_lookup_factory(my jsx, my tag == EXPR_JSX_FRAGMENT, names, errs);
            // A value tag (`<Comp />`, `<this.x />`) is a reference, an intrinsic one (`<div />`) is a name
            // (a missing name, after a parse error, is nothing)
            bool intrinsic = my tag == EXPR_JSX_FRAGMENT || jsx_element_is_intrinsic(my jsx) ||
                !my jsx->tag_name.inner_span.sv.size;
            if (!intrinsic) expr_lookup_names(&my jsx->tag_name, names, errs);
            for_each (i, vec_spanned_type, &my jsx->type_args) type_lookup_names(&i->type, names, errs);
            expr_lookup_names(&my jsx->attributes, names, errs);
            for_each (i, vec_jsx_child, &my jsx->children) {
                if (i->tag == JSX_CHILD_EXPR || i->tag == JSX_CHILD_ELEMENT) expr_lookup_names(&i->expr, names, errs);
            }
            if (!intrinsic && my jsx->closing_name.tag != EXPR_IMPLICIT_ANY && span_valid(my jsx->closing_name.inner_span) &&
                my jsx->closing_name.inner_span.sv.size
            ) {
                expr_lookup_names(&my jsx->closing_name, names, errs);
            }
        }   break;

        case EXPR_JSX_ATTRIBUTES:
            for_each (i, vec_jsx_attribute, &my jsx_attributes) {
                if (i->tag != JSX_ATTRIBUTE_NO_VALUE && i->tag != JSX_ATTRIBUTE_EMPTY_EXPR) {
                    expr_lookup_names(&i->value, names, errs);
                }
            }
            break;
        default: impossible;
    }
}

void expr_lookup_names(Expr* me, Scope* names, ErrorCtx* errs) {
    lazy_type_bind(&my lazy_type, LAZY_TYPE_EXPR);

    // What this expression is to whoever walks it: taken, so that operands only get what they are given
    FlowBuilder* flow = names->flow;
    FlowNode* true_target = flow->true_target;
    FlowNode* false_target = flow->false_target;
    bool chain_continues = flow->chain_continues;
    uint8_t assignment_target = flow->assignment_target;
    bool compound_like_target = flow->compound_like_target;
    flow->compound_like_target = false;
    bool read_target = flow->read_target;
    flow->read_target = false;
    bool nullish_condition = flow->nullish_condition;
    flow->nullish_condition = false;
    flow->true_target = nullptr;
    flow->false_target = nullptr;
    flow->chain_continues = false;
    flow->assignment_target = FLOW_TARGET_NONE;

    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
        case EXPR_LITERAL: break;

        case EXPR_IDENTIFIER:
            identifier_expr_lookup_names(&my identifier, names, errs);
            // `x = 1` doesn't read x (tsc's isWriteOnlyAccess)
            if (assignment_target != FLOW_TARGET_DEFINITE || read_target) {
                unused_mark_referenced(names, (Symbol*)my identifier.symbol, SYMBOL_VALUE);
            }
            my identifier.flow = flow_builder_reference(flow, my identifier.name);
            my identifier.assignment_target = assignment_target;
            my identifier.compound_like_target = compound_like_target;
            my identifier.assume_initialized = flow->in_type_query;
            if (flow->binding_root && my identifier.target) {
                // `const { a = "0", b = +a } = obj`
                Binding const* declared = symbol_find_node(my identifier.target, SYMBOL_DECL_VARIABLE);
                while (declared && declared->parent) declared = declared->parent;
                if (declared == flow->binding_root) my identifier.assume_initialized = true;
            }
            if (assignment_target) identifier_mark_assigned(&my identifier, names);
            if (!my identifier.keyword) {
                scope_check_strict_identifier(names, my identifier.name, false);
                if (assignment_target) scope_check_strict_eval_or_arguments(names, my identifier.name);
            }
            break;

        case EXPR_BINARY_OP: {
            BinaryOp op = my binary_op->op;
            Expr* left = &my binary_op->left;
            Expr* right = &my binary_op->right;
            // `o.m = function () {}` (or `??=` & co.): `this` in there may be `o`
            if (binary_op_is_assignment(op) && right->tag == EXPR_FUNCTION && !span_valid(right->function->arrow_span) &&
                left->tag == EXPR_PROPERTY
            ) {
                right->function->this_assigned_object = &left->property->expr;
            }
            if (binary_op_is_logical(op) || binary_op_is_logical_assignment(op)) {
                if (true_target) {
                    expr_lookup_names_logical(me, names, errs, true_target, false_target);
                } else {
                    FlowNode* post = flow_new_branch_label(flow);
                    FlowNode* saved = flow->current;
                    bool saved_effects = flow->has_flow_effects;
                    flow->has_flow_effects = false;
                    expr_lookup_names_logical(me, names, errs, post, post);
                    flow_set_current(flow, flow->has_flow_effects ? flow_finish_label(post) : saved);
                    flow->has_flow_effects |= saved_effects;
                }
            } else if (op == BINARY_OP_EQUAL && (left->tag == EXPR_ARRAY || left->tag == EXPR_OBJECT)) {
                // A destructuring assignment. Nested in another pattern (`[[a] = [1]] = e`) the default comes first
                if (assignment_target) expr_lookup_names(right, names, errs);
                flow->assignment_target = FLOW_TARGET_DEFINITE;
                expr_lookup_names(left, names, errs);
                if (!assignment_target) expr_lookup_names(right, names, errs);
                // TODO: when nested, tsc makes these nodes twice (here and from the outer pattern)
                if (!assignment_target) {
                    flow_assignment_target(flow, left, (FlowAssign){ .kind = FLOW_ASSIGN_PATTERN, .assignment = me });
                }
            } else if (binary_op_is_assignment(op)) {
                // `a = 1` as an element of a pattern is a target with a default: whoever walks the pattern assigns
                flow->assignment_target = op == BINARY_OP_EQUAL ? FLOW_TARGET_DEFINITE : FLOW_TARGET_COMPOUND;
                flow->compound_like_target = op == BINARY_OP_EQUAL && is_compound_like(right);
                expr_lookup_names(left, names, errs);
                expr_lookup_names(right, names, errs);
                if (!assignment_target) {
                    flow_assignment_target(flow, left, (FlowAssign){ .kind = FLOW_ASSIGN_EXPR, .assignment = me });
                }
            } else {
                expr_lookup_names(left, names, errs);
                if (op == BINARY_OP_COMMA) flow_maybe_call(flow, left);
                expr_lookup_names(right, names, errs);
                if (op == BINARY_OP_COMMA) flow_maybe_call(flow, right);
            }
        }   break;

        case EXPR_UNARY_OP: {
            Expr* operand = &my unary_op->expr;
            // tsc's checkStrictModeDeleteExpression
            if (my unary_op->op == UNARY_OP_DELETE && names->strict && operand->tag == EXPR_IDENTIFIER &&
                !operand->identifier.keyword && !expr_is_perenthisized(operand)
            ) {
                error_ctx_error_d(errs, operand->inner_span, 1102, "'delete' cannot be called on an identifier in strict mode.");
            }
            switch (my unary_op->op) {
                case UNARY_OP_LEFT_BANG:
                    flow->true_target = false_target;
                    flow->false_target = true_target;
                    expr_lookup_names(operand, names, errs);
                    break;

                case UNARY_OP_LEFT_PLUS_PLUS:
                case UNARY_OP_LEFT_MINUS_MINUS:
                case UNARY_OP_RIGHT_PLUS_PLUS:
                case UNARY_OP_RIGHT_MINUS_MINUS:
                    flow->assignment_target = FLOW_TARGET_COMPOUND;
                    expr_lookup_names(operand, names, errs);
                    flow_assignment_target(flow, operand, (FlowAssign){ .kind = FLOW_ASSIGN_UPDATE, .assignment = me });
                    break;

                case UNARY_OP_DELETE:
                    expr_lookup_names(operand, names, errs);
                    if (operand->tag == EXPR_PROPERTY && operand->property->form == PROPERTY_EXPR_DOT) {
                        flow_assignment_target(flow, operand, (FlowAssign){ .kind = FLOW_ASSIGN_DELETE, .assignment = me });
                    }
                    break;

                case UNARY_OP_RIGHT_BANG:
                    if (expr_is_optional_chain(me)) {
                        expr_lookup_names_chain_flow(me, names, errs, true_target, false_target, chain_continues, nullish_condition);
                    } else {
                        flow->assignment_target = assignment_target; // `x! = 1`
                        flow->read_target = true;
                        expr_lookup_names(operand, names, errs);
                        // `x!` says x was assigned, whatever control flow thinks
                        if (operand->tag == EXPR_IDENTIFIER) operand->identifier.assume_initialized = true;
                    }
                    break;

                case UNARY_OP_YIELD:
                case UNARY_OP_YIELD_STAR:
                    expr_lookup_names(operand, names, errs);
                    if (flow->function && flow->function->is_generator) {
                        my unary_op->generator = flow->function;
                        if (flow->yield_count == flow->yield_cap) {
                            flow->yield_cap = flow->yield_cap ? flow->yield_cap * 2 : 4;
                            flow->yields = realloc(flow->yields, sizeof(Expr const*) * flow->yield_cap);
                            panic_if(!flow->yields, "out of memory");
                        }
                        flow->yields[flow->yield_count++] = me;
                    }
                    break;

                default:
                    expr_lookup_names(operand, names, errs);
                    break;
            }
        }   break;

        case EXPR_PROPERTY:
            // Also when it can't be narrowed (`f().x`): the checker finds out when it makes the reference key
            my property->flow = flow_builder_reference(flow, my outer_span);
            my property->assignment_target = assignment_target;
            my property->compound_like_target = compound_like_target;
            my property->enclosing_class = scope_enclosing_class(names);
            if (my property->form == PROPERTY_EXPR_DOT && my property->prop.tag == EXPR_LITERAL &&
                span_valid(my property->prop.inner_span) && my property->prop.inner_span.sv.buf[0] == '#'
            ) {
                csview name = my property->prop.inner_span.sv;
                ClassDecl const* c = my property->enclosing_class;
                while (c && !class_decl_find_private_name(c, name)) c = c->outer_class;
                my property->private_class = c;
            }
            {
                ThisRef this_ref = scope_get_this(names, false);
                if (this_ref.kind == THIS_PARAMETER) my property->this_parameter = this_ref.symbol;
                if (this_ref.kind == THIS_ANY) my property->this_function = this_ref.function;
            }
            my property->in_constructor_of = flow->constructor_of;
            if (my property->form == PROPERTY_EXPR_DOT && my property->expr.tag != EXPR_PROPERTY) {
                my property->initializer_context = flow->initializer_context;
                my property->initializer_field = flow->initializer_field;
            }
            my property->containing_function = names->function;
            if (my property->expr.tag == EXPR_IDENTIFIER) {
                my property->expr.identifier.super_member = my property->expr.identifier.keyword == KEYWORD_IDENTIFIER_SUPER;
            }
            if (my property->expr.tag == EXPR_IDENTIFIER &&
                (my property->form == PROPERTY_EXPR_DOT || my property->prop.tag == EXPR_LITERAL)
            ) {
                my property->expr.identifier.constraint_position = true;
            }
            if (expr_is_optional_chain(me)) {
                expr_lookup_names_chain_flow(me, names, errs, true_target, false_target, chain_continues, nullish_condition);
            } else {
                expr_lookup_names(&my property->expr, names, errs);
                expr_lookup_names(&my property->prop, names, errs);
            }
            break;

        case EXPR_GENERIC_PARAMS:
            // `f<T>` is part of the call as far as flow goes
            flow->true_target = true_target;
            flow->false_target = false_target;
            flow->chain_continues = chain_continues;
            expr_lookup_names(&my generic_params->expr, names, errs);
            for_each (i, vec_spanned_type, &my generic_params->params) {
                type_lookup_names(&i->type, names, errs);
            }
            break;

        case EXPR_AS_TYPE:
            expr_lookup_names(&my as_type->expr, names, errs);
            if (!as_type_is_const_assertion(my as_type)) {
                type_lookup_names(&my as_type->type, names, errs);
            }
            break;

        case EXPR_TERNARY: {
            FlowNode* true_label = flow_new_branch_label(flow);
            FlowNode* false_label = flow_new_branch_label(flow);
            FlowNode* post = flow_new_branch_label(flow);
            FlowNode* saved = flow->current;
            bool saved_effects = flow->has_flow_effects;
            flow->has_flow_effects = false;
            expr_lookup_names_condition(&my ternary->condition, names, errs, true_label, false_label);
            flow_set_current(flow, flow_finish_label(true_label));
            expr_lookup_names(&my ternary->yes, names, errs);
            flow_add_antecedent(flow, post, flow->current);
            flow_set_current(flow, flow_finish_label(false_label));
            expr_lookup_names(&my ternary->no, names, errs);
            flow_add_antecedent(flow, post, flow->current);
            flow_set_current(flow, flow->has_flow_effects ? flow_finish_label(post) : saved);
            flow->has_flow_effects |= saved_effects;
        }   break;

        case EXPR_CALL: {
            // (Undone after the switch. A `new` expression isn't a call to tsc's isContextuallyTyped)
            if (!my call->uses_new) flow->contextual_depth++;
            mark_indirect_call(&my call->callable);
            if (my call->uses_new) my call->enclosing_class = scope_enclosing_class(names);
            if (expr_is_optional_chain(me)) {
                expr_lookup_names_chain_flow(me, names, errs, true_target, false_target, chain_continues, nullish_condition);
                break;
            }
            Expr* callee = &my call->callable;
            while (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
            if (callee->tag == EXPR_IDENTIFIER) callee->identifier.constraint_position = true;
            if (callee->tag == EXPR_FUNCTION && !my call->uses_new) {
                // An immediately invoked function expression is part of this flow, after its arguments
                for_each (i, vec_expr, &my call->args) {
                    expr_lookup_names(i, names, errs);
                }
                flow->next_function_is_iife = !callee->function->is_async && !callee->function->is_generator;
                expr_lookup_names(&my call->callable, names, errs);
                flow->next_function_is_iife = false;
                break;
            }
            if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER && !my call->uses_new) {
                callee->identifier.super_call = true;
                // (tsc's findFirstSuperCall looks in the body only)
                if (names->function && names->function->is_constructor && !names->function->first_super_call &&
                    names->no_super != NO_SUPER_CONSTRUCTOR_ARGS
                ) {
                    names->function->first_super_call = me;
                }
            }
            expr_lookup_names(&my call->callable, names, errs);
            for_each (i, vec_expr, &my call->args) {
                expr_lookup_names(i, names, errs);
            }
            if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER &&
                !my call->uses_new && !flow->dead
            ) {
                flow->current = flow_new_call(flow, flow->current, me);
            }
        }   break;

        case EXPR_FUNCTION: {
            // A function expression's name is only visible inside it
            Scope* name_scope = names;
            if (span_valid(my function->name)) {
                name_scope = scope_new(names, SCOPE_BLOCK);
                function_impl_add_names(my function, name_scope, false);
            }
            my function->syntactically_contextual = flow->contextual_depth > 0;
            function_impl_lookup_names_as(my function, FUNCTION_IS_EXPRESSION, !span_valid(my function->arrow_span), name_scope, errs);
        }   break;

        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &my object) {
                if (i->computed_key) {
                    // (A scope of its own only for resolve_super())
                    Scope* key_scope = scope_new(names, SCOPE_BLOCK);
                    key_scope->no_super = NO_SUPER_LITERAL_COMPUTED_NAME;
                    // The name of a method or accessor is in it, as far as a class initializer is concerned (TS2729)
                    uint8_t initializer_context = flow->initializer_context;
                    if (i->tag != OBJECT_ITEM_PROPERTY) flow->initializer_context = INITIALIZER_NONE;
                    expr_lookup_names(i->computed_key, key_scope, errs);
                    flow->initializer_context = initializer_context;
                }
                if (i->value.tag == EXPR_FUNCTION) {
                    i->value.function->accessor = i->tag == OBJECT_ITEM_GETTER ? 1 : i->tag == OBJECT_ITEM_SETTER ? 2 : 0;
                    if (i->tag != OBJECT_ITEM_PROPERTY) i->value.function->member_name = i->name_span;
                    // tsc's getContainingObjectLiteral: methods, accessors, `a: function () {}` (not arrows)
                    if (!span_valid(i->value.function->arrow_span)) i->value.function->this_literal = me;
                }
                if (i->tag == OBJECT_ITEM_SETTER && !i->computed_key) {
                    for_each (other, vec_object_expr_item, &my object) {
                        if (other->tag == OBJECT_ITEM_GETTER && !other->computed_key && cstr_eq(&other->name_string, &i->name_string)) {
                            function_impl_pair_setter(i->value.function, other->value.function);
                            break;
                        }
                    }
                }
                flow->assignment_target = assignment_target; // A pattern: the values are the targets
                expr_lookup_names(&i->value, names, errs);
                if (i->shorthand_default) expr_lookup_names_initializer(i->shorthand_default, names, errs);
            }
            break;

        case EXPR_ARRAY:
            for_each (i, vec_expr, &my array) {
                flow->assignment_target = assignment_target;
                expr_lookup_names(i, names, errs);
            }
            break;

        case EXPR_CLASS_DECL: {
            Scope* class_name_scope = scope_new(names, SCOPE_BLOCK);
            class_decl_add_names(my class_decl, class_name_scope, false, false);
            class_decl_lookup_names(my class_decl, class_name_scope, errs);
        }   break;

        case EXPR_SPREAD:
            flow->assignment_target = assignment_target;
            flow->read_target = true;
            expr_lookup_names(my spread, names, errs);
            break;

        case EXPR_REGEX:
            break;

        case EXPR_PRIVATE_NAME: {
            ClassDecl const* c = scope_enclosing_class(names);
            my private_name->enclosing_class = c;
            while (c && !class_decl_find_private_name(c, my inner_span.sv)) c = c->outer_class;
            my private_name->private_class = c;
        }   break;

        case EXPR_TEMPLATE:
            if (my template_->tag) {
                mark_indirect_call(my template_->tag);
                expr_lookup_names(my template_->tag, names, errs);
            }
            for_each (i, vec_expr, &my template_->substitutions) {
                expr_lookup_names(i, names, errs);
            }
            break;

        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT:
        case EXPR_JSX_ATTRIBUTES:
            jsx_lookup_names(me, names, errs);
            break;
    }
    if (my tag == EXPR_CALL && !my call->uses_new) flow->contextual_depth--;
}

static BinaryOp binary_op_without_assignment(BinaryOp op) {
    switch (op) {
        case BINARY_OP_PLUS_EQUAL: return BINARY_OP_PLUS; break;
        case BINARY_OP_STAR_STAR_EQUAL: return BINARY_OP_STAR_STAR; break;
        case BINARY_OP_STAR_EQUAL: return BINARY_OP_STAR; break;
        case BINARY_OP_SLASH_EQUAL: return BINARY_OP_SLASH; break;
        case BINARY_OP_MOD_EQUAL: return BINARY_OP_MOD; break;
        case BINARY_OP_MINUS_EQUAL: return BINARY_OP_MINUS; break;
        case BINARY_OP_LESS_LESS_EQUAL: return BINARY_OP_LESS_LESS; break;
        case BINARY_OP_GREATER_GREATER_EQUAL: return BINARY_OP_GREATER_GREATER; break;
        case BINARY_OP_GREATER_GREATER_GREATER_EQUAL: return BINARY_OP_GREATER_GREATER_GREATER; break;
        case BINARY_OP_AND_EQUAL: return BINARY_OP_AND; break;
        case BINARY_OP_XOR_EQUAL: return BINARY_OP_XOR; break;
        case BINARY_OP_OR_EQUAL: return BINARY_OP_OR; break;
        case BINARY_OP_AND_AND_EQUAL: return BINARY_OP_AND_AND; break;
        case BINARY_OP_OR_OR_EQUAL: return BINARY_OP_OR_OR; break;
        case BINARY_OP_EH_EH_EQUAL: return BINARY_OP_EH_EH; break;
        default: panic(
            "binary_op_without_assignment() called with non-assignment op ",
            binary_op_get_zv(op).str,
            " (", (i32)op, ")"
        );
    }
}

static bool extends_strict_nulls(Ty const* source, TyIntrinsic target) {
    Checker* checker = current_checker;
    return ty_is_related(checker, source, ty_intrinsic(checker, target), TY_RELATION_ASSIGNABLE_STRICT_NULLS);
}

Expr const* expr_effective_check_node(Expr const* me) {
    while (my tag == EXPR_AS_TYPE && my as_type->is_satisfies) me = &my as_type->expr;
    return me;
}

// `target = source` where target is a reference or a destructuring pattern (tsc's checkReferenceAssignment and
// checkDestructuringAssignment): each reference in it gets what it destructures to. Null `source` when that can't be
// told (TODO: rest elements, iterables that aren't arrays), which checks the shape of the target only
// `source_expr`: the right-hand side of a plain assignment, which a failure is elaborated into
static bool is_written_undefined(Expr const* me);
static void assignment_target_typecheck(Expr const* target, Ty const* source, Expr const* source_expr, ErrorCtx* errs) {
    Checker* checker = current_checker;
    if (source && (source->flags & TYF_ANY)) source = nullptr;
    switch (target->tag) {
        case EXPR_LITERAL:
            // `undefined = x`: tsc's undefinedSymbol, which isn't a variable
            if (is_written_undefined(target) && !expr_is_perenthisized(target)) {
                error_ctx_error_d(errs, target->inner_span, 2539, "Cannot assign to 'undefined' because it is not a variable.");
                return;
            }
            break;

        case EXPR_IDENTIFIER:
            // (Not assignable at all: said by identifier_expr_typecheck(), tsc's errorType)
            if (identifier_is_invalid_assignment_target(&target->identifier)) return;
            if (source && target->identifier.target) {
                Ty const* declared = ty_type_of_symbol(checker, target->identifier.target);
                if (target->identifier.compound_like_target) declared = ty_base_type_of_literal(checker, declared);
                ty_check_assignable_expr(checker, source, declared, target->outer_span, source_expr, errs);
            }
            return;

        case EXPR_PROPERTY:
            if (source) ty_check_assignable_expr(checker, source, ty_expr_type(checker, target), target->outer_span, source_expr, errs);
            return;

        case EXPR_UNARY_OP:
            // `x! = 1`
            if (target->unary_op->op == UNARY_OP_RIGHT_BANG) assignment_target_typecheck(&target->unary_op->expr, source, source_expr, errs);
            else break;
            return;

        case EXPR_AS_TYPE:
            // `(x as T) = 1`: T is what's assigned to
            if (source && !target->as_type->is_satisfies) {
                ty_check_assignable_expr(checker, source, ty_expr_type(checker, target), target->outer_span, source_expr, errs);
            }
            return;

        case EXPR_BINARY_OP:
            // `[x = 1] = a`: a default, which the walk checks as an assignment of its own
            if (target->binary_op->op != BINARY_OP_EQUAL || expr_is_perenthisized(target)) break;
            assignment_target_typecheck(
                &target->binary_op->left, source ? ty_without_undefined(checker, source) : nullptr, nullptr, errs
            );
            return;

        case EXPR_ARRAY: {
            if (expr_is_perenthisized(target)) break;
            bool array_like = source && ty_extends_array(checker, source);
            // What isn't an array has to be iterable (tsc's checkArrayLiteralAssignment)
            Ty const* iterated = source && !array_like ?
                ty_iterated_type(checker, ITERATION_DESTRUCTURING, source, target->outer_span, errs) : nullptr;
            u32 index = 0;
            for_each (i, vec_expr, &target->array) {
                Ty const* element = iterated;
                if (i->tag == EXPR_SPREAD) {
                    assignment_target_typecheck(i->spread, nullptr, nullptr, errs); // TODO: the rest of the array or tuple
                } else if (i->tag == EXPR_LITERAL && i->outer_span.sv.size == 0) {
                    // A hole
                } else {
                    if (array_like) {
                        Value key = value_number(index);
                        element = ty_access(checker, source, ty_literal(checker, &key));
                        if (!element) element = ty_access(checker, source, ty_intrinsic(checker, TY_NUMBER));
                    }
                    assignment_target_typecheck(i, element, nullptr, errs);
                }
                index++;
            }
            return;
        }

        case EXPR_OBJECT:
            if (expr_is_perenthisized(target)) break;
            for_each (i, vec_object_expr_item, &target->object) {
                Ty const* property = nullptr;
                if (source && (i->tag == OBJECT_ITEM_PROPERTY || i->tag == OBJECT_ITEM_SHORTHAND) && !i->computed_key) {
                    property = ty_access(checker, source, ty_string_literal(checker, cstr_sv(&i->name_string)));
                    ty_mark_destructured_member(checker, source, cstr_sv(&i->name_string));
                }
                if (i->tag == OBJECT_ITEM_SPREAD) {
                    assignment_target_typecheck(&i->value, nullptr, nullptr, errs); // TODO: the rest of the object
                } else if (i->tag == OBJECT_ITEM_SHORTHAND && i->shorthand_default) {
                    // HACK: `{ a = 1 } = o`: the parser drops the default, so neither side is checked
                } else if (i->tag == OBJECT_ITEM_PROPERTY || i->tag == OBJECT_ITEM_SHORTHAND) {
                    assignment_target_typecheck(&i->value, property, nullptr, errs);
                }
            }
            return;

        default:
            break;
    }
    error_ctx_error_d(errs, target->outer_span, 2364,
        "The left-hand side of an assignment expression must be a variable or a property access.");
}

// tsc's PredicateSemantics: whether an expression is truthy (or nullish) always, never or sometimes
typedef enum PredicateSemantics {
    PREDICATE_ALWAYS = 1,
    PREDICATE_NEVER = 2,
    PREDICATE_SOMETIMES = PREDICATE_ALWAYS | PREDICATE_NEVER,
} PredicateSemantics;

// tsc's skipOuterExpressions: parentheses (implicit here), assertions, `!` and instantiation expressions
static Expr const* skip_outer_expressions(Expr const* me) {
    for (;;) {
        if (my tag == EXPR_AS_TYPE) me = &my as_type->expr;
        else if (my tag == EXPR_UNARY_OP && my unary_op->op == UNARY_OP_RIGHT_BANG) me = &my unary_op->expr;
        else if (my tag == EXPR_GENERIC_PARAMS) me = &my generic_params->expr;
        else return me;
    }
}

// The global `undefined` (tsc's undefinedSymbol), which the parser makes a literal. Other undefined literals stand in
// for missing expressions (`for (;;)`)
static bool is_written_undefined(Expr const* me) {
    return my tag == EXPR_LITERAL && my literal.tag == VALUE_UNDEFINED && csview_equals(my inner_span.sv, "undefined");
}

// tsc's getSyntacticTruthySemantics
static PredicateSemantics truthy_semantics(Expr const* me) {
    me = skip_outer_expressions(me);
    switch (my tag) {
        case EXPR_LITERAL:
            switch (my literal.tag) {
                case VALUE_NUMBER: // `while (1)` is fine
                    return my literal.number == 0 || my literal.number == 1 ? PREDICATE_SOMETIMES : PREDICATE_ALWAYS;
                case VALUE_BIGINT: return PREDICATE_ALWAYS;
                case VALUE_NULL: return PREDICATE_NEVER;
                case VALUE_STRING: return cstr_size(&my literal.string) ? PREDICATE_ALWAYS : PREDICATE_NEVER;
                case VALUE_UNDEFINED: return is_written_undefined(me) ? PREDICATE_NEVER : PREDICATE_SOMETIMES;
                default: return PREDICATE_SOMETIMES;
            }
        case EXPR_ARRAY:
        case EXPR_FUNCTION:
        case EXPR_CLASS_DECL:
        case EXPR_OBJECT:
        case EXPR_REGEX:
            return PREDICATE_ALWAYS;
        case EXPR_UNARY_OP:
            return my unary_op->op == UNARY_OP_VOID ? PREDICATE_NEVER : PREDICATE_SOMETIMES;
        case EXPR_TERNARY:
            return truthy_semantics(&my ternary->yes) | truthy_semantics(&my ternary->no);
        default:
            return PREDICATE_SOMETIMES;
    }
}

static Span node_error_span(Expr const* me);

Span expr_error_span(Expr const* me) {
    return expr_is_perenthisized(me) ? my outer_span : node_error_span(me);
}

// expr_error_span() of what is in the parentheses, if any
static Span node_error_span(Expr const* me) {
    Span span = my inner_span;
    char const* start = span_start_ptr(span);
    switch (my tag) {
        case EXPR_AS_TYPE:
            return my as_type->is_satisfies ? my as_type->keyword_span : span;
        case EXPR_FUNCTION: {
            FunctionImpl const* function = my function;
            if (span_valid(function->arrow_span)) {
                // Only the first line of a block body that spans several
                char const* end = span_end_ptr(span);
                if (function->expression_body) return span;
                char const* line_end = span_end_ptr(function->arrow_span);
                while (line_end < end && *line_end != '\n' && *line_end != '\r') line_end++;
                return line_end < end ? span_make(span.source, start, line_end) : span;
            }
            if (span_valid(function->name)) return function->name;
            break;
        }
        case EXPR_CLASS_DECL:
            if (span_valid(my class_decl->name)) return my class_decl->name;
            break;
        default:
            return span;
    }
    // The first token (`function`, `async` or `class`)
    char const* end = start;
    while (end < span_end_ptr(span) && (isalnum((unsigned char)*end) || *end == '_' || *end == '$')) end++;
    return span_make(span.source, start, end);
}

// tsc's checkTruthinessOfType: a condition can't be void, nor something that is obviously always truthy or falsy
void expr_check_truthiness(Expr const* me, ErrorCtx* errs) {
    if (ty_expr_type(current_checker, me)->flags & TYF_VOID) {
        error_ctx_error_d(errs, expr_error_span(me), 1345, "An expression of type 'void' cannot be tested for truthiness.");
        return;
    }
    PredicateSemantics semantics = truthy_semantics(me);
    if (semantics == PREDICATE_ALWAYS) {
        error_ctx_error_d(errs, expr_error_span(me), 2872, "This kind of expression is always truthy.");
    } else if (semantics == PREDICATE_NEVER) {
        error_ctx_error_d(errs, expr_error_span(me), 2873, "This kind of expression is always falsy.");
    }
}

// tsc's getSyntacticNullishnessSemantics
static PredicateSemantics nullish_semantics(Expr const* me) {
    me = skip_outer_expressions(me);
    switch (my tag) {
        case EXPR_CALL:
        case EXPR_PROPERTY:
        case EXPR_IMPLICIT_ANY:
            return PREDICATE_SOMETIMES;
        case EXPR_TEMPLATE:
            return my template_->tag ? PREDICATE_SOMETIMES : PREDICATE_NEVER;
        case EXPR_UNARY_OP:
            switch (my unary_op->op) {
                case UNARY_OP_AWAIT: case UNARY_OP_YIELD: case UNARY_OP_YIELD_STAR: return PREDICATE_SOMETIMES;
                default: return PREDICATE_NEVER;
            }
        case EXPR_BINARY_OP:
            switch (my binary_op->op) {
                case BINARY_OP_EQUAL: case BINARY_OP_EH_EH: case BINARY_OP_EH_EH_EQUAL: case BINARY_OP_OR_OR:
                case BINARY_OP_OR_OR_EQUAL: case BINARY_OP_AND_AND: case BINARY_OP_AND_AND_EQUAL:
                    return PREDICATE_SOMETIMES;
                case BINARY_OP_COMMA:
                    return nullish_semantics(&my binary_op->right);
                default:
                    return PREDICATE_NEVER;
            }
        case EXPR_TERNARY:
            return nullish_semantics(&my ternary->yes) | nullish_semantics(&my ternary->no);
        case EXPR_LITERAL:
            if (my literal.tag == VALUE_NULL || is_written_undefined(me)) return PREDICATE_ALWAYS;
            return my literal.tag == VALUE_UNDEFINED ? PREDICATE_SOMETIMES : PREDICATE_NEVER;
        case EXPR_IDENTIFIER:
            return my identifier.keyword == KEYWORD_IDENTIFIER_SUPER ? PREDICATE_NEVER : PREDICATE_SOMETIMES;
        default:
            return PREDICATE_NEVER;
    }
}

// tsc's checkNullishCoalesceOperands: `??` doesn't mix with `||`/`&&` without parentheses (TS5076), and its left
// operand has to be possibly nullish, as far as its syntax tells
static void nullish_coalesce_typecheck(Expr const* me, ErrorCtx* errs) {
    Expr const* operands[] = { &my binary_op->left, &my binary_op->right };
    for (u32 i = 0; i < 2; i++) {
        Expr const* operand = operands[i];
        if (operand->tag != EXPR_BINARY_OP || expr_is_perenthisized(operand)) continue;
        BinaryOp op = operand->binary_op->op;
        if ((op != BINARY_OP_OR_OR && op != BINARY_OP_AND_AND) || !operand->outer_span.source || operand->outer_span.source->has_parse_errors) continue;
        error_ctx_error_d(errs, operand->outer_span, 5076, "'", binary_op_get_zv(op).str,
            "' and '?" "?' operations cannot be mixed without parentheses.");
    }
    Expr const* left = skip_outer_expressions(&my binary_op->left);
    PredicateSemantics semantics = nullish_semantics(left);
    if (semantics == PREDICATE_SOMETIMES) return;
    if (my binary_op->is_operand) {
        error_ctx_error_d(errs, node_error_span(left), 2870, "This binary expression is never nullish. Are you missing parentheses?");
    } else if (semantics == PREDICATE_ALWAYS) {
        error_ctx_error_d(errs, node_error_span(left), 2871, "This expression is always nullish.");
    } else {
        error_ctx_error_d(errs, node_error_span(left), 2869,
            "Right operand of ?? is unreachable because the left operand is never nullish.");
    }
}

// An object or array literal that is a destructuring assignment target: its elements are assigned to
static bool is_destructuring_target(Expr const* me) {
    switch (my tag) {
        case EXPR_IDENTIFIER: return my identifier.assignment_target;
        case EXPR_PROPERTY: return my property->assignment_target;
        case EXPR_BINARY_OP: return my binary_op->op == BINARY_OP_EQUAL && is_destructuring_target(&my binary_op->left);
        case EXPR_SPREAD: return is_destructuring_target(my spread);
        case EXPR_ARRAY:
            for_each (i, vec_expr, &my array) {
                if (is_destructuring_target(i)) return true;
            }
            return false;
        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &my object) {
                if (i->shorthand_default || is_destructuring_target(&i->value)) return true;
            }
            return false;
        default:
            return false;
    }
}

// tsc's checkGrammarObjectLiteralExpression: a name can be one property, one method or one get/set pair
static void object_check_duplicate_names(Expr const* me, ErrorCtx* errs) {
    if (!my outer_span.source || my outer_span.source->has_parse_errors || is_destructuring_target(me)) return;
    enum { GET = 1, SET = 2, PROPERTY = 4, METHOD = 8, ACCESSOR = GET | SET };
    ObjectExpr const* object = &my object;
    isize count = vec_object_expr_item_size(object);
    uint8_t* seen = calloc(count ? (size_t)count : 1, 1);
    panic_if(!seen, "out of memory");
    for (isize n = 0; n < count; n++) {
        ObjectExprItem const* item = vec_object_expr_item_at(object, n);
        uint8_t kind;
        switch (item->tag) {
            case OBJECT_ITEM_PROPERTY: case OBJECT_ITEM_SHORTHAND: kind = PROPERTY; break;
            case OBJECT_ITEM_METHOD: kind = METHOD; break;
            case OBJECT_ITEM_GETTER: kind = GET; break;
            case OBJECT_ITEM_SETTER: kind = SET; break;
            default: continue;
        }
        if (item->computed_key) continue; // TODO: literal-typed computed names (tsc's tryGetNameFromType)
        isize existing = -1;
        for (isize j = 0; j < n && existing < 0; j++) {
            ObjectExprItem const* other = vec_object_expr_item_at(object, j);
            if (seen[j] && !other->computed_key && cstr_equals_sv(&other->name_string, cstr_sv(&item->name_string))) existing = j;
        }
        if (existing < 0) {
            seen[n] = kind;
            continue;
        }
        uint8_t previous = seen[existing];
        Span name = item->name_span;
        if ((kind & METHOD) && (previous & METHOD)) {
            error_ctx_error_d(errs, name, 2300, "Duplicate identifier '", name, "'.");
        } else if ((kind & PROPERTY) && (previous & PROPERTY)) {
            error_ctx_error_d(errs, name, 1117, "An object literal cannot have multiple properties with the same name.");
        } else if ((kind & ACCESSOR) && (previous & ACCESSOR)) {
            if (previous != ACCESSOR && kind != previous) {
                seen[existing] = kind | previous;
            } else {
                error_ctx_error_d(errs, name, 1118, "An object literal cannot have multiple get/set accessors with the same name.");
                break;
            }
        } else {
            error_ctx_error_d(errs, name, 1119, "An object literal cannot have property and accessor with the same name.");
            break;
        }
    }
    free(seen);
}

// tsc's isSideEffectFree
static bool is_side_effect_free(Expr const* me) {
    switch (my tag) {
        case EXPR_IDENTIFIER:
            return !my identifier.keyword;
        case EXPR_LITERAL:
        case EXPR_REGEX:
        case EXPR_TEMPLATE:
        case EXPR_FUNCTION:
        case EXPR_CLASS_DECL:
        case EXPR_ARRAY:
        case EXPR_OBJECT:
        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT:
            return true;
        case EXPR_TERNARY:
            return is_side_effect_free(&my ternary->yes) && is_side_effect_free(&my ternary->no);
        case EXPR_BINARY_OP:
            if (binary_op_is_assignment(my binary_op->op)) return false;
            return is_side_effect_free(&my binary_op->left) && is_side_effect_free(&my binary_op->right);
        case EXPR_UNARY_OP:
            switch (my unary_op->op) {
                case UNARY_OP_LEFT_BANG: case UNARY_OP_PLUS: case UNARY_OP_MINUS: case UNARY_OP_TILDE:
                case UNARY_OP_TYPEOF: case UNARY_OP_RIGHT_BANG:
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

// tsc's checkPrefixUnaryExpression/checkPostfixUnaryExpression for `++`/`--`: the operand has to be numeric, and then
// a reference (tsc's checkReferenceExpression)
static void increment_typecheck(Expr const* operand, ErrorCtx* errs) {
    Checker* checker = current_checker;
    Ty const* type = ty_check_non_null_type(checker, ty_expr_type(checker, operand), operand, errs);
    Ty const* numeric[] = { ty_intrinsic(checker, TY_NUMBER), ty_intrinsic(checker, TY_BIGINT) };
    Ty const* number_or_bigint = ty_union(checker, numeric, 2, nullptr, (TyList){});
    if (type && !ty_is_related(checker, type, number_or_bigint, TY_RELATION_ASSIGNABLE)) {
        error_ctx_error_d(errs, operand->outer_span, 2356,
            "An arithmetic operand must be of type 'any', 'number', 'bigint' or an enum type.");
        return;
    }
    Expr const* reference = operand;
    while (reference->tag == EXPR_AS_TYPE || (reference->tag == EXPR_UNARY_OP && reference->unary_op->op == UNARY_OP_RIGHT_BANG)) {
        reference = reference->tag == EXPR_AS_TYPE ? &reference->as_type->expr : &reference->unary_op->expr;
    }
    if ((reference->tag != EXPR_IDENTIFIER || reference->identifier.keyword) && reference->tag != EXPR_PROPERTY) {
        error_ctx_error_d(errs, operand->outer_span, 2357,
            "The operand of an increment or decrement operator must be a variable or a property access.");
    } else if (expr_is_optional_chain(reference)) {
        error_ctx_error_d(errs, operand->outer_span, 2777,
            "The operand of an increment or decrement operator may not be an optional property access.");
    }
}

// tsc's checkYieldExpression: a `yield*` operand has to be iterable, and what is yielded has to be what the annotation
// says the generator yields
static void yield_typecheck(Expr const* me, ErrorCtx* errs) {
    Checker* checker = current_checker;
    UnaryOpExpr const* yield = my unary_op;
    FunctionImpl const* generator = yield->generator;
    if (!generator) return;
    bool async = generator->is_async;
    if (yield->op == UNARY_OP_YIELD_STAR) {
        ScriptTarget target = compiler_options()->target;
        Span keyword = my outer_span; // (what tsc reports it at)
        if (keyword.sv.size > 5) keyword.sv.size = 5;
        if (async && target < SCRIPT_TARGET_ES2018) {
            ty_check_emit_helpers(checker, keyword,
                EMIT_HELPER_AWAIT | EMIT_HELPER_ASYNC_DELEGATOR | EMIT_HELPER_ASYNC_VALUES, errs);
        }
        if (!async && target < SCRIPT_TARGET_ES2015 && compiler_options()->downlevel_iteration) {
            ty_check_emit_helpers(checker, keyword, EMIT_HELPER_VALUES, errs);
        }
    }
    Ty const* yielded;
    Span span = yield->expr.tag == EXPR_IMPLICIT_ANY ? my outer_span : yield->expr.outer_span;
    if (yield->op == UNARY_OP_YIELD_STAR) {
        u32 use = async ? ITERATION_ASYNC_YIELD_STAR : ITERATION_YIELD_STAR;
        yielded = ty_iterated_type(checker, use, ty_expr_type(checker, &yield->expr), span, errs);
    } else {
        yielded = yield->expr.tag == EXPR_IMPLICIT_ANY ? ty_intrinsic(checker, TY_UNDEFINED) : ty_expr_type(checker, &yield->expr);
    }
    if (async) yielded = ty_awaited(checker, yielded);
    if (!generator->ret_explicit) return;
    TyIterationTypes const* declared = ty_generator_context(checker, generator);
    if (declared) {
        ty_check_assignable_expr(checker, yielded, declared->yield_type, span,
            yield->op == UNARY_OP_YIELD_STAR || yield->expr.tag == EXPR_IMPLICIT_ANY ? nullptr : &yield->expr, errs);
    }
}

// tsc's isTypeAssignableToKind (`strict`: any, unknown, void, null and undefined are nothing of the kind)
static bool assignable_to_kind(Ty const* ty, u32 kind, bool strict) {
    Checker* checker = current_checker;
    if (ty->flags & kind) return true;
    if (strict && (ty->flags & (TYF_ANY | TYF_UNKNOWN | TYF_VOID | TYF_NULLABLE))) return false;
    if ((kind & (TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM)) &&
        ty_is_related(checker, ty, ty_intrinsic(checker, TY_NUMBER), TY_RELATION_ASSIGNABLE)
    ) {
        return true;
    }
    if ((kind & (TYF_BIGINT | TYF_BIGINT_LITERAL)) && ty_is_related(checker, ty, ty_intrinsic(checker, TY_BIGINT), TY_RELATION_ASSIGNABLE)) {
        return true;
    }
    return (kind & TYF_STRING_LIKE) && ty_is_related(checker, ty, ty_intrinsic(checker, TY_STRING), TY_RELATION_ASSIGNABLE);
}

// tsc's maybeTypeOfKind (`constraint`: of a type parameter, its constraint)
static bool maybe_kind(Ty const* ty, u32 kind, bool constraint) {
    if (ty->flags & kind) return true;
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (maybe_kind(ty->composite.types.data[i], kind, constraint)) return true;
        }
        return false;
    }
    if (constraint && (ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED))) {
        // (tsc's maybeTypeOfKindConsideringBaseConstraint)
        Ty const* base = ty_base_constraint(current_checker, ty);
        return base && base != ty && maybe_kind(base, kind, false);
    }
    return false;
}

// tsc's getBaseTypeOfLiteralTypeForComparison: enum and number literals are numbers, string-likes strings
static Ty const* base_type_for_comparison(Checker* checker, Ty const* ty) {
    if (ty->kind == TY_UNION) {
        Ty const* stack[16];
        TyList types = ty->composite.types;
        if (types.len > c_arraylen(stack)) return ty;
        for (u32 i = 0; i < types.len; i++) stack[i] = base_type_for_comparison(checker, types.data[i]);
        return ty_union(checker, stack, types.len, nullptr, (TyList){});
    }
    if (ty->flags & (TYF_STRING_LITERAL | TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) return ty_intrinsic(checker, TY_STRING);
    if (ty->flags & (TYF_NUMBER_LITERAL | TYF_ENUM)) return ty_intrinsic(checker, TY_NUMBER);
    if (ty->flags & TYF_BIGINT_LITERAL) return ty_intrinsic(checker, TY_BIGINT);
    if (ty->flags & TYF_BOOLEAN_LITERAL) return ty_intrinsic(checker, TY_BOOLEAN);
    return ty;
}

// The operands of `<` & co.: both numeric, or both not and comparable
static bool comparison_operands_fit(Ty const* left, Ty const* right) {
    Checker* checker = current_checker;
    if ((left->flags | right->flags) & TYF_ANY) return true;
    Ty const* numeric_types[] = { ty_intrinsic(checker, TY_NUMBER), ty_intrinsic(checker, TY_BIGINT) };
    Ty const* number_or_bigint = ty_union(checker, numeric_types, 2, nullptr, (TyList){});
    bool left_numeric = ty_is_related(checker, left, number_or_bigint, TY_RELATION_ASSIGNABLE);
    bool right_numeric = ty_is_related(checker, right, number_or_bigint, TY_RELATION_ASSIGNABLE);
    return (left_numeric && right_numeric) || (!left_numeric && !right_numeric && ty_is_comparable_either_way(checker, left, right));
}

// tsc's reportOperatorError with getBaseTypesIfUnrelated: literal types are shown as their base types if those don't
// fit either, the operands fitting when both are of `kind`
static void report_operator_error(Ty const* left, Ty const* right, Expr const* expr, u32 kind, ErrorCtx* errs) {
    Checker* checker = current_checker;
    Ty const* left_base = ty_base_type_of_literal(checker, left);
    Ty const* right_base = ty_base_type_of_literal(checker, right);
    bool related = assignable_to_kind(left_base, kind, false) && assignable_to_kind(right_base, kind, false);
    if (!related) {
        left = left_base;
        right = right_base;
    }
    BinaryOp written = expr->binary_op->op;
    error_ctx_error_d(errs, expr->inner_span, 2365, "Operator '", binary_op_get_zv(written).str, "' cannot be applied to types '",
        left, "' and '", right, "'.");
}

// The right side of `instanceof`: callable, constructable or with a [Symbol.hasInstance] (of every union member, some
// intersection member)
static bool instanceof_target_ok(Checker* checker, Ty const* ty) {
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        bool is_union = ty->kind == TY_UNION;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            bool fits = instanceof_target_ok(checker, ty->composite.types.data[i]);
            if (fits != is_union) return fits;
        }
        return is_union;
    }
    Ty const* apparent = ty_apparent_type(checker, ty);
    if (!(apparent->flags & TYF_OBJECT)) return false;
    Members const* members = ty_members(checker, apparent);
    if (members->call_count || members->construct_count) return true;
    for (u32 i = 0; i < members->prop_count; i++) {
        // (A well-known symbol's escaped name: `__@hasInstance@<id>`)
        csview name = atom_sv(members->props[i]->name);
        for (isize c = 0; c + 12 <= name.size; c++) {
            if (memcmp(name.buf + c, "@hasInstance", 12) == 0) return true;
        }
    }
    return false;
}

// tsc's checkForDisallowedESSymbolOperand: symbols don't convert to numbers or strings implicitly
static bool check_symbol_operands(Ty const* left, Ty const* right, Expr const* expr, ErrorCtx* errs) {
    Expr const* offending = maybe_kind(left, TYF_ES_SYMBOL_LIKE, true) ? &expr->binary_op->left :
        maybe_kind(right, TYF_ES_SYMBOL_LIKE, true) ? &expr->binary_op->right : nullptr;
    if (!offending) return true;
    error_ctx_error_d(errs, expr_error_span(offending), 2469, "The '", binary_op_get_zv(expr->binary_op->op).str,
        "' operator cannot be applied to type 'symbol'.");
    return false;
}

static void binary_op_typecheck(
    Ty const* left,
    BinaryOp op,
    Ty const* right,
    Expr const* expr,
    ErrorCtx* errs
) {
    switch (op) {
        case BINARY_OP_PLUS: {
            Checker* checker = current_checker;
            // (tsc's isTypeAssignableToKind: without strictNullChecks null and undefined are strings too)
            if (!ty_is_related(checker, left, ty_intrinsic(checker, TY_STRING), TY_RELATION_ASSIGNABLE) &&
                !ty_is_related(checker, right, ty_intrinsic(checker, TY_STRING), TY_RELATION_ASSIGNABLE)
            ) {
                // Null or undefined operands are an error of their own (tsc's checkNonNullType), then any
                left = ty_check_non_null_type(checker, left, &expr->binary_op->left, errs);
                right = ty_check_non_null_type(checker, right, &expr->binary_op->right, errs);
                if (!left || !right) break;
            }
            u32 number_like = TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM, bigint_like = TYF_BIGINT | TYF_BIGINT_LITERAL;
            u32 string_like = TYF_STRING_LIKE;
            if ((assignable_to_kind(left, number_like, true) && assignable_to_kind(right, number_like, true)) ||
                (assignable_to_kind(left, bigint_like, true) && assignable_to_kind(right, bigint_like, true)) ||
                assignable_to_kind(left, string_like, true) || assignable_to_kind(right, string_like, true) ||
                ((left->flags | right->flags) & TYF_ANY)
            ) {
                check_symbol_operands(left, right, expr, errs);
                break;
            }
            // Shown as their base types (`false` as `boolean`) unless both could plausibly be operands
            u32 close_enough = number_like | bigint_like | string_like | TYF_ANY | TYF_UNKNOWN;
            report_operator_error(left, right, expr, close_enough, errs);
        }   break;

        case BINARY_OP_STAR_STAR:
        case BINARY_OP_STAR:
        case BINARY_OP_SLASH:
        case BINARY_OP_MOD:
        case BINARY_OP_MINUS:
        case BINARY_OP_LESS_LESS:
        case BINARY_OP_GREATER_GREATER:
        case BINARY_OP_GREATER_GREATER_GREATER:
        case BINARY_OP_AND:
        case BINARY_OP_XOR:
        case BINARY_OP_OR: {
            Checker* checker = current_checker;
            left = ty_check_non_null_type(checker, left, &expr->binary_op->left, errs);
            right = ty_check_non_null_type(checker, right, &expr->binary_op->right, errs);
            Ty const* any = ty_intrinsic(checker, TY_ANY);
            if (!left) left = any; // (tsc's errorType)
            if (!right) right = any;
            // `true & false`: probably meant `&&`
            BinaryOp suggested = op == BINARY_OP_AND ? BINARY_OP_AND_AND : op == BINARY_OP_OR ? BINARY_OP_OR_OR :
                op == BINARY_OP_XOR ? BINARY_OP_TRIPLE_NOT_EQUAL : op;
            if (suggested != op && maybe_kind(left, TYF_BOOLEAN | TYF_BOOLEAN_LITERAL, true) &&
                maybe_kind(right, TYF_BOOLEAN | TYF_BOOLEAN_LITERAL, true)
            ) {
                error_ctx_error_d(errs, expr->inner_span, 2447, "The '", binary_op_get_zv(expr->binary_op->op).str,
                    "' operator is not allowed for boolean types. Consider using '", binary_op_get_zv(suggested).str, "' instead.");
                break;
            }
            Ty const* numeric_types[] = { ty_intrinsic(checker, TY_NUMBER), ty_intrinsic(checker, TY_BIGINT) };
            Ty const* number_or_bigint = ty_union(checker, numeric_types, 2, nullptr, (TyList){});
            if (!ty_is_related(checker, left, number_or_bigint, TY_RELATION_ASSIGNABLE)) {
                error_ctx_error_d(errs, expr->binary_op->left.outer_span, 2362,
                    "The left-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type.");
            }
            if (!ty_is_related(checker, right, number_or_bigint, TY_RELATION_ASSIGNABLE)) {
                error_ctx_error_d(errs, expr->binary_op->right.outer_span, 2363,
                    "The right-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type.");
            }
            // Mixing bigint with anything else
            u32 bigint_like = TYF_BIGINT | TYF_BIGINT_LITERAL;
            bool both_any = assignable_to_kind(left, TYF_ANY | TYF_UNKNOWN, false) && assignable_to_kind(right, TYF_ANY | TYF_UNKNOWN, false);
            if (both_any || !(maybe_kind(left, bigint_like, false) || maybe_kind(right, bigint_like, false))) break;
            if (assignable_to_kind(left, bigint_like, false) && assignable_to_kind(right, bigint_like, false)) {
                if (op == BINARY_OP_GREATER_GREATER_GREATER) {
                    report_operator_error(left, right, expr, bigint_like, errs);
                } else if (op == BINARY_OP_STAR_STAR && compiler_options()->target < SCRIPT_TARGET_ES2016) {
                    error_ctx_error_d(errs, expr->inner_span, 2791,
                        "Exponentiation cannot be performed on 'bigint' values unless the 'target' option is set to 'es2016' or later.");
                }
            } else {
                report_operator_error(left, right, expr, bigint_like, errs);
            }
        }   break;

        case BINARY_OP_LESS:
        case BINARY_OP_GREATER:
        case BINARY_OP_LESS_EQUAL:
        case BINARY_OP_GREATER_EQUAL: {
            Checker* checker = current_checker;
            if (!check_symbol_operands(left, right, expr, errs)) break;
            left = ty_check_non_null_type(checker, left, &expr->binary_op->left, errs);
            right = ty_check_non_null_type(checker, right, &expr->binary_op->right, errs);
            if (!left || !right) break;
            left = base_type_for_comparison(checker, left);
            right = base_type_for_comparison(checker, right);
            if ((left->flags | right->flags) & TYF_ANY) break;
            if (comparison_operands_fit(left, right)) break;
            // (tsc's getBaseTypesIfUnrelated)
            Ty const* left_base = ty_base_type_of_literal(checker, left);
            Ty const* right_base = ty_base_type_of_literal(checker, right);
            if (!comparison_operands_fit(left_base, right_base)) {
                left = left_base;
                right = right_base;
            }
            error_ctx_error_d(errs, expr->inner_span, 2365, "Operator '", binary_op_get_zv(op).str,
                "' cannot be applied to types '", left, "' and '", right, "'.");
        }   break;

        case BINARY_OP_EQUAL:
            assignment_target_typecheck(&expr->binary_op->left, right, &expr->binary_op->right, errs);
            break;

        case BINARY_OP_DOUBLE_EQUAL:
        case BINARY_OP_TRIPLE_EQUAL:
        case BINARY_OP_NOT_EQUAL:
        case BINARY_OP_TRIPLE_NOT_EQUAL:
            // (Anything may be compared with null and undefined: tsc's isTypeEqualityComparableTo())
            if (!((left->flags | right->flags) & TYF_NULLABLE) && !ty_is_comparable_either_way(current_checker, left, right)) {
                // Shown as their base types if those don't overlap either (`n == b` for a b narrowed to `false`)
                Ty const* left_base = ty_base_type_of_literal(current_checker, left);
                Ty const* right_base = ty_base_type_of_literal(current_checker, right);
                if (!ty_is_comparable_either_way(current_checker, left_base, right_base)) {
                    left = left_base;
                    right = right_base;
                }
                error_ctx_error_d(errs, expr->outer_span, 2367,
                    "This comparison appears to be unintentional because the types '", left,
                    "' and '", right, "' have no overlap.");
            }
            break;

        case BINARY_OP_INSTANCEOF: {
            // tsc's checkInstanceOfExpression: the left side can't be a primitive
            Checker* checker = current_checker;
            u32 primitive = TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT | TYF_BIGINT_LITERAL | TYF_BOOLEAN |
                TYF_BOOLEAN_LITERAL | TYF_ENUM | TYF_ES_SYMBOL_LIKE | TYF_VOID | TYF_NULLABLE;
            TyList parts = left->kind == TY_UNION ? left->composite.types : (TyList){ .data = &left, .len = 1 };
            // HACK: not in JS files, whose JSDoc types tttt doesn't read
            bool all_primitive = !(left->flags & (TYF_ANY | TYF_NEVER)) && !(expr->inner_span.source && expr->inner_span.source->is_js);
            for (u32 i = 0; i < parts.len && all_primitive; i++) all_primitive = assignable_to_kind(parts.data[i], primitive, false);
            if (all_primitive) {
                error_ctx_error_d(errs, expr->binary_op->left.outer_span, 2358, "The left-hand side of an 'instanceof' "
                    "expression must be of type 'any', an object type or a type parameter.");
            }
            // (tsc's resolveInstanceofExpression) the right side something callable or constructable
            if (right->flags & TYF_ANY) break;
            if (instanceof_target_ok(checker, right)) break;
            Ty const* function_type = ty_global_function_type(checker);
            if (!function_type || ty_is_related(checker, right, function_type, TY_RELATION_ASSIGNABLE)) break;
            error_ctx_error_d(errs, expr->binary_op->right.outer_span, 2359, "The right-hand side of an 'instanceof' "
                "expression must be either of type 'any', a class, function, or other type assignable to the 'Function' "
                "interface type, or an object type with a 'Symbol.hasInstance' method.");
        }   break;

        case BINARY_OP_IN:
            ty_check_in_expression(current_checker, &expr->binary_op->left, &expr->binary_op->right, errs);
            break;

        case BINARY_OP_AND_AND:
        case BINARY_OP_OR_OR:
            break;

        case BINARY_OP_PLUS_EQUAL:
        case BINARY_OP_STAR_STAR_EQUAL:
        case BINARY_OP_STAR_EQUAL:
        case BINARY_OP_SLASH_EQUAL:
        case BINARY_OP_MOD_EQUAL:
        case BINARY_OP_MINUS_EQUAL:
        case BINARY_OP_LESS_LESS_EQUAL:
        case BINARY_OP_GREATER_GREATER_EQUAL:
        case BINARY_OP_GREATER_GREATER_GREATER_EQUAL:
        case BINARY_OP_AND_EQUAL:
        case BINARY_OP_XOR_EQUAL:
        case BINARY_OP_OR_EQUAL:
        case BINARY_OP_AND_AND_EQUAL:
        case BINARY_OP_OR_OR_EQUAL:
        case BINARY_OP_EH_EH_EQUAL: {
            BinaryOp without_assignment = binary_op_without_assignment(op);
            binary_op_typecheck(left, without_assignment, right, expr, errs);
            // The expression's own type is what gets assigned
            binary_op_typecheck(left, BINARY_OP_EQUAL, expr_get_type((Expr*)expr), expr, errs);
        }   break;

        case BINARY_OP_COMMA:
        case BINARY_OP_EH_EH:
            break;
    }
}

Ty const* expr_evaluate_type(Expr const* me) {
    // Nobody checked this expression with a contextual type first, see ty/expr.c
    return ty_compute_expr_without_context(current_checker, me);
}

// The flags of a regular expression literal (the flag part of tsc's reScanSlashToken). TODO: the pattern itself
// (scanRegularExpressionWorker: TS1487, TS1508, TS1535...)
// (not inlined: expr_typecheck() recurses as deep as expressions nest, see binderBinaryExpressionStress)
__attribute__((noinline)) static void regex_check_flags(Expr const* me, ErrorCtx* errs) {
    csview text = my inner_span.sv;
    isize end = text.size;
    while (end > 0 && text.buf[end - 1] != '/') end--;
    if (end <= 1) return;
    static char const letters[] = "dgimsuvy";
    // What each flag needs: `u` and `y` ES2015, `s` ES2018, `d` ES2022, `v` ES2024
    static ScriptTarget const available[] = {
        SCRIPT_TARGET_ES2022, 0, 0, 0, SCRIPT_TARGET_ES2018, SCRIPT_TARGET_ES2015, SCRIPT_TARGET_ES2024, SCRIPT_TARGET_ES2015,
    };
    static char const* const names[] = { [SCRIPT_TARGET_ES2015] = "es6", [SCRIPT_TARGET_ES2018] = "es2018",
        [SCRIPT_TARGET_ES2022] = "es2022", [SCRIPT_TARGET_ES2024] = "es2024" };
    u32 seen = 0;
    for (isize i = end; i < text.size; i++) {
        Span at = { .source = my inner_span.source, .sv = csview_from_n(text.buf + i, 1) };
        char const* found = strchr(letters, text.buf[i]);
        if (!found || !text.buf[i]) {
            error_ctx_grammar_error_d(errs, at, 1499, "Unknown regular expression flag.");
            continue;
        }
        u32 flag = 1u << (found - letters);
        u32 unicode_modes = (1u << 5) | (1u << 6);
        if (seen & flag) {
            error_ctx_grammar_error_d(errs, at, 1500, "Duplicate regular expression flag.");
        } else if (((seen | flag) & unicode_modes) == unicode_modes) {
            error_ctx_grammar_error_d(errs, at, 1502,
                "The Unicode (u) flag and the Unicode Sets (v) flag cannot be set simultaneously.");
        } else {
            seen |= flag;
            ScriptTarget needs = available[found - letters];
            if (needs && compiler_options()->target < needs) {
                error_ctx_grammar_error_d(errs, at, 1501, "This regular expression flag is only available when targeting '",
                    names[needs], "' or later.");
            }
        }
    }
}

// The target of a destructuring assignment (`[...x] = e`, `({ ...x } = e)`), as the resolve walk marked it
static bool expr_is_destructuring_target(Expr const* me) {
    return (my tag == EXPR_IDENTIFIER && my identifier.assignment_target) ||
        (my tag == EXPR_PROPERTY && my property->assignment_target);
}

// Private names are emitted with WeakMaps and the __classPrivateField* helpers
static bool private_names_lowered(void) {
    return compiler_options()->target < SCRIPT_TARGET_ESNEXT || !compiler_options()->use_define_for_class_fields;
}

static void private_access_check_emit_helpers(Expr const* me, ErrorCtx* errs) {
    if (!private_names_lowered()) return;
    uint8_t target = my property->assignment_target;
    if (target != FLOW_TARGET_NONE) ty_check_emit_helpers(current_checker, my outer_span, EMIT_HELPER_CLASS_PRIVATE_FIELD_SET, errs);
    if (target != FLOW_TARGET_DEFINITE) ty_check_emit_helpers(current_checker, my outer_span, EMIT_HELPER_CLASS_PRIVATE_FIELD_GET, errs);
}

// Not inlined into expr_typecheck(), whose frame deeply nested expressions pay for (binderBinaryExpressionStress)
__attribute__((noinline)) static void jsx_typecheck(Expr const* me, ErrorCtx* errs) {
    switch (my tag) {
        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT: {
            // tsc's checkJsxPreconditions
            if (compiler_options()->jsx == JSX_NONE) {
                error_ctx_error_d(errs, my jsx->opening_span, 17004, "Cannot use JSX unless the '--jsx' flag is provided.");
            }
            bool intrinsic = my tag == EXPR_JSX_FRAGMENT || jsx_element_is_intrinsic(my jsx) ||
                !my jsx->tag_name.inner_span.sv.size;
            if (!intrinsic) expr_typecheck(&my jsx->tag_name, errs);
            for_each (i, vec_spanned_type, &my jsx->type_args) type_typecheck(&i->type, errs);
            if (span_valid(my jsx->type_args_trailing_comma)) {
                error_ctx_grammar_error_d(errs, my jsx->type_args_trailing_comma, 1009, "Trailing comma not allowed.");
            }
            if (span_valid(my jsx->type_args_empty)) {
                error_ctx_grammar_error_d(errs, my jsx->type_args_empty, 1099, "Type argument list cannot be empty.");
            }
            // (What checks the attributes with their contextual type, before the walk gets to them)
            ty_check_jsx_element(current_checker, me, errs);
            expr_typecheck(&my jsx->attributes, errs);
            for_each (i, vec_jsx_child, &my jsx->children) {
                if (i->tag == JSX_CHILD_EXPR || i->tag == JSX_CHILD_ELEMENT) expr_typecheck(&i->expr, errs);
            }
        }   break;

        case EXPR_JSX_ATTRIBUTES:
            for_each (i, vec_jsx_attribute, &my jsx_attributes) {
                if (i->tag != JSX_ATTRIBUTE_NO_VALUE && i->tag != JSX_ATTRIBUTE_EMPTY_EXPR) {
                    expr_typecheck(&i->value, errs);
                }
            }
            break;

        default: impossible;
    }
}

void expr_typecheck(Expr const* me, ErrorCtx* errs) {
    // The expression's own type first: evaluating it is what gives the expressions inside it their contextual types
    // (whoever knows this expression's contextual type has done the same before getting here)
    expr_get_type((Expr*)me);
    switch (my tag) {
        case EXPR_IMPLICIT_ANY: break;
        case EXPR_LITERAL: break;

        case EXPR_IDENTIFIER: identifier_expr_typecheck(&my identifier, errs); break;

        case EXPR_BINARY_OP:
            if (my binary_op->op == BINARY_OP_IN && my binary_op->left.tag == EXPR_PRIVATE_NAME &&
                !expr_is_perenthisized(&my binary_op->left)
            ) {
                // The one place `#x` is an expression
                if (private_names_lowered()) {
                    ty_check_emit_helpers(current_checker, my binary_op->left.outer_span, EMIT_HELPER_CLASS_PRIVATE_FIELD_IN, errs);
                }
                expr_typecheck(&my binary_op->right, errs);
                ty_check_in_expression(current_checker, &my binary_op->left, &my binary_op->right, errs);
                break;
            }
            expr_typecheck(&my binary_op->left, errs);
            if (my binary_op->op == BINARY_OP_AND_AND || my binary_op->op == BINARY_OP_OR_OR) {
                expr_check_truthiness(&my binary_op->left, errs);
            } else if (my binary_op->op == BINARY_OP_EH_EH) {
                nullish_coalesce_typecheck(me, errs);
            } else if (my binary_op->op == BINARY_OP_COMMA && !compiler_options()->allow_unreachable_code &&
                !my binary_op->indirect_call && !my binary_op->jsx_siblings && is_side_effect_free(&my binary_op->left)
            ) {
                error_ctx_error_d(errs, expr_error_span(&my binary_op->left), 2695,
                    "Left side of comma operator is unused and has no side effects.");
            }
            expr_typecheck(&my binary_op->right, errs);
            binary_op_typecheck(
                expr_get_type(&my binary_op->left),
                my binary_op->op,
                expr_get_type(&my binary_op->right),
                me,
                errs
            );
            break;

        case EXPR_UNARY_OP:
            expr_typecheck(&my unary_op->expr, errs);
            switch (my unary_op->op) {
                case UNARY_OP_LEFT_BANG:
                    expr_check_truthiness(&my unary_op->expr, errs);
                    break;

                case UNARY_OP_TILDE:
                case UNARY_OP_PLUS:
                case UNARY_OP_MINUS: {
                    Ty const* operand = ty_expr_type(current_checker, &my unary_op->expr);
                    ty_check_non_null_type(current_checker, operand, &my unary_op->expr, errs);
                    if (maybe_kind(operand, TYF_ES_SYMBOL_LIKE, true)) {
                        csview op_text = csview_trim(zsview_sv(unary_op_get_zv(my unary_op->op)));
                        error_ctx_error_d(errs, expr_error_span(&my unary_op->expr), 2469, "The '", op_text,
                            "' operator cannot be applied to type 'symbol'.");
                    }
                }   break;

                case UNARY_OP_TYPEOF:
                case UNARY_OP_VOID:
                case UNARY_OP_AWAIT:
                    // TODO: add typechecking
                    break;

                case UNARY_OP_DELETE:
                    ty_check_delete_operand(current_checker, &my unary_op->expr, errs);
                    break;

                case UNARY_OP_LEFT_PLUS_PLUS:
                case UNARY_OP_LEFT_MINUS_MINUS:
                case UNARY_OP_RIGHT_PLUS_PLUS:
                case UNARY_OP_RIGHT_MINUS_MINUS:
                    increment_typecheck(&my unary_op->expr, errs);
                    break;

                case UNARY_OP_YIELD:
                case UNARY_OP_YIELD_STAR:
                    yield_typecheck(me, errs);
                    break;

                case UNARY_OP_RIGHT_BANG:
                    break;
            }
            break;

        case EXPR_PROPERTY: {
            expr_typecheck(&my property->expr, errs);
            expr_typecheck(&my property->prop, errs);
            Ty const* expr_type = expr_get_type(&my property->expr);
            // HACK: the rest of a chain (`a?.b.c`) only runs when `a?.b` did, but its type can't tell the `undefined`
            // of the short circuit from one that `b` has itself (tsc: the optionalType marker)
            if (my property->optional_chain || expr_is_optional_chain(&my property->expr)) {
                expr_type = ty_non_nullable(current_checker, expr_type);
            }
            bool error_if_not_found = my property->form == PROPERTY_EXPR_DOT;
            if (my property->form == PROPERTY_EXPR_DOT && my property->prop.tag == EXPR_LITERAL &&
                span_valid(my property->prop.inner_span) && my property->prop.inner_span.sv.buf[0] == '#'
            ) {
                private_access_check_emit_helpers(me, errs);
            }
            if (ty_check_private_name_access(current_checker, my property, expr_type, errs)) break;
            ty_check_property_access(
                current_checker,
                expr_type,
                ty_access_key_type(current_checker, my property),
                my property->expr.outer_span,
                my property->prop.outer_span,
                expr_is_entity_name(&my property->expr),
                error_if_not_found,
                errs
            );
            ty_check_member_modifiers(current_checker, my property, expr_type, my inner_span, errs);
        }   break;

        case EXPR_GENERIC_PARAMS: generic_params_expr_typecheck(my generic_params, errs); break;

        case EXPR_AS_TYPE:
            if (!my as_type->is_satisfies && !as_type_is_const_assertion(my as_type)) {
                // The asserted type is the contextual type of the operand, which nobody has looked at yet
                Ty const* asserted = ty_of_node(&my as_type->type);
                ty_check_expr(current_checker, &my as_type->expr, asserted->flags & TYF_ANY ? nullptr : asserted);
            }
            expr_typecheck(&my as_type->expr, errs);
            type_typecheck(&my as_type->type, errs);
            if (!my as_type->is_satisfies && !as_type_is_const_assertion(my as_type)) {
                ty_check_type_assertion(current_checker, expr_get_type(&my as_type->expr), ty_of_node(&my as_type->type),
                    my inner_span, errs);
            }
            if (my as_type->is_satisfies) {
                Checker* checker = current_checker;
                ty_check_satisfies(checker, expr_get_type(&my as_type->expr), ty_of_node(&my as_type->type),
                    my as_type->keyword_span, &my as_type->expr, errs);
            }
            break;

        case EXPR_TERNARY:
            expr_typecheck(&my ternary->condition, errs);
            expr_check_truthiness(&my ternary->condition, errs);
            expr_typecheck(&my ternary->yes, errs);
            expr_typecheck(&my ternary->no, errs);
            break;

        case EXPR_CALL: {
            Expr const* callee = &my call->callable;
            if (callee->tag == EXPR_GENERIC_PARAMS) {
                // Explicit type arguments are the call's business (ty/call.c)
                for_each (i, vec_spanned_type, &callee->generic_params->params) {
                    type_typecheck(&i->type, errs);
                }
                callee = &callee->generic_params->expr;
            }
            expr_typecheck(callee, errs);
            // Resolving the call gives the arguments their contextual types, so it comes before checking them
            Checker* checker = current_checker;
            LazyMemo* memo = lazy_type_state_peek((LazyType*)&my lazy_type);
            Ty const* contextual = memo && memo->done ? memo->context : nullptr;
            Ty const* callee_type = ty_expr_type(checker, callee);
            // `f?.()` doesn't call the undefined, and neither does `a?.b()` the undefined of a chain that ended, see
            // chain_object_type()
            if (my call->optional_chain || (expr_is_optional_chain(callee) && !expr_is_perenthisized(callee))) {
                callee_type = ty_non_nullable(checker, callee_type);
            }
            ty_call_typecheck(checker, callee_type, my call, contextual, my outer_span, errs);
            for_each (i, vec_expr, &my call->args) {
                expr_typecheck(i, errs);
            }
        }   break;

        case EXPR_FUNCTION:
            function_impl_typecheck(my function, errs);
            break;

        case EXPR_OBJECT:
            object_check_duplicate_names(me, errs);
            for_each (i, vec_object_expr_item, &my object) {
                if (i->computed_key) {
                    expr_typecheck(i->computed_key, errs);
                    ty_check_computed_name(current_checker, i->computed_key, i->name_span, false, errs);
                }
                expr_typecheck(&i->value, errs);
                if (i->tag == OBJECT_ITEM_SPREAD) {
                    // `{ ...o }` is emitted with __assign below ES2015, `({ ...x } = o)` with __rest below ES2018
                    ScriptTarget target = compiler_options()->target;
                    if (expr_is_destructuring_target(&i->value)) {
                        if (target < SCRIPT_TARGET_ES2018) ty_check_emit_helpers(current_checker, i->span, EMIT_HELPER_REST, errs);
                    } else if (target < SCRIPT_TARGET_ES2015) {
                        ty_check_emit_helpers(current_checker, i->span, EMIT_HELPER_ASSIGN, errs);
                    }
                }
            }
            break;

        case EXPR_ARRAY:
            for_each (i, vec_expr, &my array) {
                expr_typecheck(i, errs);
            }
            break;

        case EXPR_CLASS_DECL:
            class_decl_typecheck(my class_decl, errs);
            break;

        case EXPR_SPREAD: {
            expr_typecheck(my spread, errs);
            // `...x` of an array literal or a call: what isn't an array has to be iterable. Not in a destructuring
            // assignment, where `x` is assigned to (`[...x] = e`)
            Checker* checker = current_checker;
            bool target = expr_is_destructuring_target(my spread);
            if (!target && compiler_options()->target < SCRIPT_TARGET_ES2015) {
                ty_check_emit_helpers(checker, my outer_span,
                    EMIT_HELPER_SPREAD_ARRAY | (compiler_options()->downlevel_iteration ? EMIT_HELPER_READ : 0), errs);
            }
            Ty const* operand = expr_get_type(my spread);
            if (!target && !(operand->flags & TYF_ANY) && !ty_extends_array(checker, operand)) {
                ty_iterated_type(checker, ITERATION_SPREAD, operand, my spread->outer_span, errs);
            }
        }   break;

        case EXPR_REGEX:
            regex_check_flags(me, errs);
            break;

        case EXPR_PRIVATE_NAME:
            // tsc's checkGrammarPrivateIdentifierExpression: an expression only as the left operand of `in`
            if (!my private_name->enclosing_class) {
                error_ctx_grammar_error_d(errs, my inner_span, 18016, "Private identifiers are not allowed outside class bodies.");
            } else {
                error_ctx_grammar_error_d(errs, my inner_span, 1451, "Private identifiers are only allowed in class bodies "
                    "and may only be used as part of a class member declaration, property access, or on the left-hand-side of an "
                    "'in' expression");
            }
            break;

        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT:
        case EXPR_JSX_ATTRIBUTES:
            jsx_typecheck(me, errs);
            break;

        case EXPR_TEMPLATE:
            if (my template_->tag) {
                if (compiler_options()->target < SCRIPT_TARGET_ES2015) {
                    ty_check_emit_helpers(current_checker, my inner_span, EMIT_HELPER_MAKE_TEMPLATE_OBJECT, errs);
                }
                expr_typecheck(my template_->tag, errs);
            }
            for_each (i, vec_expr, &my template_->substitutions) {
                expr_typecheck(i, errs);
                // tsc's checkTemplateExpression
                if (!my template_->tag && maybe_kind(ty_expr_type(current_checker, i), TYF_ES_SYMBOL_LIKE, true)) {
                    error_ctx_error_d(errs, expr_error_span(i), 2731, "Implicit conversion of a 'symbol' to a 'string' will "
                        "fail at runtime. Consider wrapping this expression in 'String(...)'.");
                }
            }
            break;
    }
}

// See https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Operator_precedence
int binary_op_get_precedence(BinaryOp op) {
    switch (op) {
        case BINARY_OP_STAR_STAR:
            return 130;

        case BINARY_OP_STAR:
        case BINARY_OP_SLASH:
        case BINARY_OP_MOD:
            return 120;

        case BINARY_OP_PLUS:
        case BINARY_OP_MINUS:
            return 110;

        case BINARY_OP_LESS_LESS:
        case BINARY_OP_GREATER_GREATER:
        case BINARY_OP_GREATER_GREATER_GREATER:
            return 100;

        case BINARY_OP_LESS:
        case BINARY_OP_LESS_EQUAL:
        case BINARY_OP_GREATER:
        case BINARY_OP_GREATER_EQUAL:
        case BINARY_OP_IN:
        case BINARY_OP_INSTANCEOF:
            return 90;

        case BINARY_OP_DOUBLE_EQUAL:
        case BINARY_OP_NOT_EQUAL:
        case BINARY_OP_TRIPLE_EQUAL:
        case BINARY_OP_TRIPLE_NOT_EQUAL:
            return 80;

        case BINARY_OP_AND:
            return 70;

        case BINARY_OP_XOR:
            return 60;

        case BINARY_OP_OR:
            return 50;

        case BINARY_OP_AND_AND:
            return 40;

        case BINARY_OP_OR_OR:
            return 30;

        // Below `||` (tsc's Coalesce): `a ?? b || c` is `a ?? (b || c)`, which is an error anyway (TS5076)
        case BINARY_OP_EH_EH:
            return 25;

        case BINARY_OP_EQUAL:
        case BINARY_OP_PLUS_EQUAL:
        case BINARY_OP_MINUS_EQUAL:
        case BINARY_OP_STAR_STAR_EQUAL:
        case BINARY_OP_STAR_EQUAL:
        case BINARY_OP_SLASH_EQUAL:
        case BINARY_OP_MOD_EQUAL:
        case BINARY_OP_LESS_LESS_EQUAL:
        case BINARY_OP_GREATER_GREATER_EQUAL:
        case BINARY_OP_GREATER_GREATER_GREATER_EQUAL:
        case BINARY_OP_AND_EQUAL:
        case BINARY_OP_XOR_EQUAL:
        case BINARY_OP_OR_EQUAL:
        case BINARY_OP_AND_AND_EQUAL:
        case BINARY_OP_OR_OR_EQUAL:
        case BINARY_OP_EH_EH_EQUAL:
            return 20;

        case BINARY_OP_COMMA:
            return 10;
    }
    impossible;
}

zsview binary_op_get_zv(BinaryOp op) {
    switch (op) {
        case BINARY_OP_STAR_STAR: return c_zv("**");
        case BINARY_OP_STAR: return c_zv("*");
        case BINARY_OP_SLASH: return c_zv("/");
        case BINARY_OP_MOD: return c_zv("%");
        case BINARY_OP_PLUS: return c_zv("+");
        case BINARY_OP_MINUS: return c_zv("-");
        case BINARY_OP_LESS_LESS: return c_zv("<<");
        case BINARY_OP_GREATER_GREATER: return c_zv(">>");
        case BINARY_OP_GREATER_GREATER_GREATER: return c_zv(">>>");
        case BINARY_OP_LESS: return c_zv("<");
        case BINARY_OP_LESS_EQUAL: return c_zv("<=");
        case BINARY_OP_GREATER: return c_zv(">");
        case BINARY_OP_GREATER_EQUAL: return c_zv(">=");
        case BINARY_OP_IN: return c_zv("in");
        case BINARY_OP_INSTANCEOF: return c_zv("instanceof");
        case BINARY_OP_DOUBLE_EQUAL: return c_zv("==");
        case BINARY_OP_NOT_EQUAL: return c_zv("!=");
        case BINARY_OP_TRIPLE_EQUAL: return c_zv("===");
        case BINARY_OP_TRIPLE_NOT_EQUAL: return c_zv("!==");
        case BINARY_OP_AND: return c_zv("&");
        case BINARY_OP_XOR: return c_zv("^");
        case BINARY_OP_OR: return c_zv("|");
        case BINARY_OP_AND_AND: return c_zv("&&");
        case BINARY_OP_OR_OR: return c_zv("||");
        case BINARY_OP_EH_EH: return c_zv("\?\?");
        case BINARY_OP_EQUAL: return c_zv("=");
        case BINARY_OP_PLUS_EQUAL: return c_zv("+=");
        case BINARY_OP_MINUS_EQUAL: return c_zv("-=");
        case BINARY_OP_STAR_STAR_EQUAL: return c_zv("**=");
        case BINARY_OP_STAR_EQUAL: return c_zv("*=");
        case BINARY_OP_SLASH_EQUAL: return c_zv("/=");
        case BINARY_OP_MOD_EQUAL: return c_zv("%=");
        case BINARY_OP_LESS_LESS_EQUAL: return c_zv("<<=");
        case BINARY_OP_GREATER_GREATER_EQUAL: return c_zv(">>=");
        case BINARY_OP_GREATER_GREATER_GREATER_EQUAL: return c_zv(">>>=");
        case BINARY_OP_AND_EQUAL: return c_zv("&=");
        case BINARY_OP_XOR_EQUAL: return c_zv("^=");
        case BINARY_OP_OR_EQUAL: return c_zv("|=");
        case BINARY_OP_AND_AND_EQUAL: return c_zv("&&=");
        case BINARY_OP_OR_OR_EQUAL: return c_zv("||=");
        case BINARY_OP_EH_EH_EQUAL: return c_zv("\?\?=");
        case BINARY_OP_COMMA: return c_zv(",");
    }
    log_error("Invalid binary operator ", (i32)op);
    return c_zv("<INVALID_OP>");
}

// See https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Operator_precedence
int unary_op_get_precedence(UnaryOp op) {
    switch (op) {
        case UNARY_OP_LEFT_BANG:
        case UNARY_OP_TYPEOF:
        case UNARY_OP_VOID:
        case UNARY_OP_TILDE:
        case UNARY_OP_PLUS:
        case UNARY_OP_MINUS:
        case UNARY_OP_LEFT_PLUS_PLUS:
        case UNARY_OP_LEFT_MINUS_MINUS:
        case UNARY_OP_DELETE:
        case UNARY_OP_AWAIT:
            return 140;

        case UNARY_OP_YIELD:
        case UNARY_OP_YIELD_STAR:
            return 20; // Same as assignment

        case UNARY_OP_RIGHT_PLUS_PLUS:
        case UNARY_OP_RIGHT_MINUS_MINUS:
        case UNARY_OP_RIGHT_BANG:
            return 150;
    }
    impossible;
}

zsview unary_op_get_zv(UnaryOp op) {
    switch (op) {
        case UNARY_OP_LEFT_BANG: return c_zv("!");
        case UNARY_OP_TYPEOF: return c_zv("typeof ");
        case UNARY_OP_VOID: return c_zv("void ");
        case UNARY_OP_TILDE: return c_zv("~");
        case UNARY_OP_PLUS: return c_zv("+");
        case UNARY_OP_MINUS: return c_zv("-");
        case UNARY_OP_LEFT_PLUS_PLUS: return c_zv("++");
        case UNARY_OP_LEFT_MINUS_MINUS: return c_zv("--");
        case UNARY_OP_DELETE: return c_zv("delete ");
        case UNARY_OP_AWAIT: return c_zv("await ");
        case UNARY_OP_YIELD: return c_zv("yield ");
        case UNARY_OP_YIELD_STAR: return c_zv("yield* ");
        case UNARY_OP_RIGHT_PLUS_PLUS: return c_zv("++");
        case UNARY_OP_RIGHT_MINUS_MINUS: return c_zv("--");
        case UNARY_OP_RIGHT_BANG: return c_zv("!");
    }
    log_error("Invalid unary operator ", (i32)op);
    return c_zv("<INVALID_OP>");
}

void object_expr_item_drop(ObjectExprItem* me) {
    cstr_drop(&my name_string);
    if (my computed_key) {
        expr_drop(my computed_key);
        free(my computed_key);
    }
    if (my shorthand_default) {
        expr_drop(my shorthand_default);
        free(my shorthand_default);
    }
    expr_drop(&my value);
}
