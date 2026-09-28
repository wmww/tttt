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
#include "regex_check.h"

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

// Whether a use of a class is in an ES decorator of the class or of one of its members, not deferred by a function
// (an IIFE runs right away): the class isn't initialized yet (tsc's isBlockScopedNameDeclaredBeforeUse)
static bool class_decorator_runs_first(Scope const* names, void const* class_decl) {
    if (compiler_options()->experimental_decorators) return false;
    for (Scope const* i = names; i; i = i->parent) {
        if (i->kind == SCOPE_FUNCTION && !(i->function && i->function->is_iife)) return false;
        if (i->decorator_of) return i->decorator_of == class_decl;
        if (i->no_super == NO_SUPER_DECORATOR) {
            Scope const* class_scope = i;
            while (class_scope && !class_scope->this_ref.class_decl) class_scope = class_scope->parent;
            return class_scope && class_scope->this_ref.class_decl == class_decl;
        }
    }
    return false;
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
            if (in_computed_name && i && i->this_ref.class_decl == decl->node) {
                // (reported below)
            } else if (!class_decorator_runs_first(names, decl->node)) {
                return;
            }
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
    // (An `export type *` has no symbol: what it brings is the name itself)
    Symbol const* named = item && item->symbol ? item->symbol : found;
    if (!item || !(symbol_resolved_flags(named) & SYMBOL_VALUE)) return;
    bool exported = port_item_is_export(item);
    Error* error = error_ctx_error_d(errs, my name, exported ? 1362 : 1361, "'", my name, "' cannot be used as a value "
        "because it was ", exported ? "exported using 'export type'." : "imported using 'import type'.");
    if (error) {
        // (tsc's node: the whole `export type *` declaration, the `* as ns` of a namespace export, the whole
        // `import A = require()`, the import clause of a default import, the whole specifier)
        Span at = item->decl_name;
        if (item->tag == PORT_REEXPORT_SPLAT || item->tag == PORT_REEXPORT_GATHER || item->tag == PORT_IMPORT_ALIAS ||
            item->is_require
        ) {
            at = item->full_span;
        } else if (item->tag == PORT_IMPORT_DEFAULT && item->type_only) {
            // (from its `type`)
            char const* start = span_start_ptr(item->decl_name);
            char const* file_start = item->decl_name.source ? cstr_str(&item->decl_name.source->contents) : start;
            while (start > file_start && (start[-1] == ' ' || start[-1] == '\t')) start--;
            if (start - file_start >= 4 && !memcmp(start - 4, "type", 4)) {
                at = span_make(item->decl_name.source, start - 4, span_end_ptr(item->decl_name));
            }
        } else if ((item->tag == PORT_IMPORT_SPECIFIC || item->tag == PORT_EXPORT_SPECIFIC || item->tag == PORT_REEXPORT_SPECIFIC) &&
            span_valid(item->source_name) && span_start_ptr(item->source_name) < span_start_ptr(item->decl_name)
        ) {
            at = item->source_name;
        }
        error_add_related_d(error, at, exported ? 1377 : 1376, "'", my name, "' was ",
            exported ? "exported" : "imported", " here.");
    }
}

// What there is to say about a module name that an import-like construct uses
void module_ref_report(ModuleRef* ref, Scope* names, ErrorCtx* errs) {
    if (!ref) return;
    if (ref->file) {
        module_ref_report_resolved(ref, errs);
    } else if (!binder_module_of_ref(names->binder, ref)) {
        module_ref_report_unresolved(ref, errs);
    }
}

// What an entity name expression (`a`, `a.b.c`) names with `meaning`, quietly: null if nothing
static Symbol const* entity_name_symbol(Scope* names, Expr const* expr, u32 meaning) {
    if (expr->tag == EXPR_IDENTIFIER && !expr->identifier.keyword) {
        return symbol_for_meaning(scope_resolve(names, expr->identifier.name.sv, meaning), meaning);
    }
    if (expr->tag != EXPR_PROPERTY || expr->property->form != PROPERTY_EXPR_DOT) return nullptr;
    Expr const* name = &expr->property->prop;
    if (name->tag != EXPR_LITERAL || name->literal.tag != VALUE_STRING) return nullptr;
    Symbol const* container = entity_name_symbol(names, &expr->property->expr, SYMBOL_NAMESPACE);
    if (!container || container->is_placeholder) return nullptr;
    return symbol_for_meaning(symbol_get_export(container, cstr_sv(&name->literal.string)), meaning);
}

// A class's `extends` names an interface (tsc's checkAndReportErrorForExtendingInterface)
static bool entity_names_interface(Scope* names, Expr const* entity) {
    return entity_name_symbol(names, entity, SYMBOL_INTERFACE) != nullptr;
}

void identifier_expr_lookup_names(IdentifierExpr* me, Scope* names, ErrorCtx* errs) {
    if (my keyword) {
        if (my keyword == KEYWORD_IDENTIFIER_THIS) {
            ThisRef this_ref = scope_get_this(names, false);
            my this_kind = this_ref.kind;
            my target = this_ref.symbol;
            my this_function = this_ref.function;
            check_before_super(me, names, 17009, "'this'", errs);
            // tsc's checkThisInStaticClassFieldInitializerInDecoratedClass: up to the this container (through arrows)
            for (Scope const* scope = names; scope && !scope->this_ref.kind; scope = scope->parent) {
                if (scope->kind == SCOPE_FUNCTION && !(scope->function && span_valid(scope->function->arrow_span))) break;
                if (!scope->decorated_static_field) continue;
                error_ctx_error_d(errs, my name, 2816, "Cannot use 'this' in a static property initializer of a decorated class.");
                break;
            }
            // tsc's checkThisExpression: the this container (arrow functions count) is a class member's computed name
            for (Scope const* scope = names; scope && scope->kind != SCOPE_FUNCTION && !scope->this_ref.kind; scope = scope->parent) {
                if (scope->no_super != NO_SUPER_COMPUTED_NAME) continue;
                error_ctx_error_d(errs, my name, 2465, "'this' cannot be referenced in a computed property name.");
                break;
            }
            if (!this_ref.kind) {
                // At the top level: of a script `typeof globalThis`, of a module `undefined`; in a namespace an error
                Scope const* file = names;
                while (file->parent && file->kind != SCOPE_MODULE && file->kind != SCOPE_SCRIPT && file->kind != SCOPE_NAMESPACE &&
                    file->kind != SCOPE_ENUM
                ) {
                    file = file->parent;
                }
                if (file->kind == SCOPE_SCRIPT) my this_kind = THIS_GLOBAL;
                if (file->kind == SCOPE_MODULE && file->file && file->file->is_commonjs) my this_kind = THIS_COMMONJS;
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
    if (my target && names->file && names->file->is_commonjs && span_equals(my name, "exports") &&
        my symbol == symbol_table_get(&names->binder->globals, my name.sv)
    ) {
        // A global `exports` doesn't hide the module (tsc's resolveNameHelper tries the file before the globals)
        my target = my symbol = nullptr;
    }
    if (my require_call && my target && names->file && names->file->is_js && expr_symbol_is_commonjs_require(my target)) {
        // An ambient `require` (@types/node's) is tsc's requireSymbol too (isCommonJsRequire)
        if (my require_call == 1) module_ref_report(my module_ref, names, errs);
        return;
    }
    if (!my target && names->file && names->file->is_js) {
        if (my require_call) {
            // tsc's requireSymbol, and the module like an import's (a declaration's aliases report it themselves)
            my target = my symbol = binder_require_symbol(names->binder);
            if (my require_call == 1) module_ref_report(my module_ref, names, errs);
            return;
        }
        if (names->file->is_commonjs && span_equals(my name, "exports")) {
            // The module itself (tsc's resolveNameHelper: `exports` in a CommonJS file is the file's symbol)
            my target = my symbol = names->file->scope->container;
            return;
        }
    }
    if (my target) {
        ClassItem const* field = scope_invalid_initializer_field(names, my name.sv, my symbol);
        if (field) {
            // Unresolved (any) like in tsc's resolveNameHelper, but the symbols baseline still shows what the name
            // finds (getSymbolAtLocation doesn't report)
            scope_report_invalid_initializer_reference(field, my name, errs);
            my target = nullptr;
            my invalid_initializer_reference = true;
            return;
        }
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
    } else if (names->flow && names->flow->type_only_alias_ok &&
        (my symbol = scope_resolve(names, my name.sv, SYMBOL_ANY_MEANING))
    ) {
        // `export = N`, `export default N`: a name exports whatever it means (tsc's checkExportAssignment resolves it
        // with every meaning), a namespace or a type is fine. No target: it has no value type
    } else if (my extends_entity && entity_names_interface(names, my extends_entity)) {
        // (tsc's checkAndReportErrorForExtendingInterface)
        error_ctx_error_d(errs, my name, 2689, "Cannot extend an interface '", my extends_entity->outer_span,
            "'. Did you mean 'implements'?");
        my symbol = scope_resolve(names, my name.sv, SYMBOL_ANY_MEANING);
        my namespace_as_value = my extends_entity->tag != EXPR_IDENTIFIER;
    } else if (scope_resolve(names, my name.sv, SYMBOL_NAMESPACE_MODULE)) {
        // (tsc's checkAndReportErrorForUsingNamespaceAsTypeOrValue)
        error_ctx_error_d(errs, my name, 2708, "Cannot use namespace '", my name, "' as a value.");
        my symbol = scope_resolve(names, my name.sv, SYMBOL_NAMESPACE_MODULE);
    } else if (scope_get_type(names, my name.sv, nullptr)) {
        // (tsc's isES2015OrLaterConstructorName)
        static char const* const later[] = { "Promise", "Symbol", "Map", "WeakMap", "Set", "WeakSet" };
        bool later_lib = false;
        for (u32 i = 0; i < c_arraylen(later); i++) later_lib |= csview_equals(my name.sv, later[i]);
        if (later_lib) {
            error_ctx_error_d(errs, my name, 2585, "'", my name, "' only refers to a type, but is being used as a value "
                "here. Do you need to change your target library? Try changing the 'lib' compiler option to es2015 or later.");
        } else if (my class_base && is_primitive_type_name(my name.sv)) {
            error_ctx_error_d(errs, my name, 2863, "A class cannot extend a primitive type like '", my name,
                "'. Classes can only extend constructable values.");
        } else {
            error_ctx_error_d(errs, my name, 2693, "'", my name, "' only refers to a type, but is being used as a value here.");
        }
    } else if (!my shorthand_unchecked) {
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
        !(my this_function && (ty_js_this_class(my this_function) || ty_contextual_this_type(current_checker, my this_function)))
    ) {
        error_ctx_error_d(errs, my name, 2683, "'this' implicitly has type 'any' because it does not have a type annotation.");
    }
}

// `f<A, B>` outside of a call (there the type arguments are the call's business, ty/call.c)
void generic_params_expr_typecheck(GenericParamsExpr* me, ErrorCtx* errs) {
    expr_typecheck(&my expr, errs);
    for_each (i, vec_spanned_type, &my params) {
        type_typecheck(&i->type, errs);
    }
    Checker* checker = current_checker;
    Ty const* callee = ty_expr_type(checker, &my expr);
    if (callee->flags & TYF_ANY) return;
    // The first signature that takes this many type arguments (TODO: all of them, like ty_apply_type_arguments())
    Members const* members = ty_members(checker, callee);
    Signature const* sig = members->call_count ? members->call[0] : members->construct_count ? members->construct[0] : nullptr;
    u32 written = (u32)vec_spanned_type_size(&my params);
    for (u32 i = 0; i < members->call_count + members->construct_count; i++) {
        Signature const* each = i < members->call_count ? members->call[i] : members->construct[i - members->call_count];
        signature_resolve(checker, each);
        u32 min = 0;
        for (u32 j = 0; j < each->type_params.len; j++) {
            if (!ty_param_default(checker, each->type_params.data[j])) min = j + 1;
        }
        if (written >= min && written <= each->type_params.len) {
            sig = each;
            break;
        }
    }
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
            if (my binary_op->jsdoc_type) {
                type_drop(my binary_op->jsdoc_type);
                free(my binary_op->jsdoc_type);
            }
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

void await_check_grammar(AwaitPlace place, AwaitKind kind, Span at, FunctionImpl const* function, ErrorCtx* errs) {
    static char const* const what[] = { "'await' expressions", "'for await' loops", "'await using' statements" };
    if (place == AWAIT_IN_FUNCTION) {
        Error* e = error_ctx_grammar_error_d(errs, at, kind == AWAIT_KIND_EXPR ? 1308 : kind == AWAIT_KIND_FOR ? 1103 : 2852,
            what[kind], " are only allowed within async functions and at the top levels of modules.");
        if (e && function && !function->is_constructor) {
            error_add_related_d(e, function_error_span(function), 1356, "Did you mean to mark this function as 'async'?");
        }
        return;
    }
    if (place != AWAIT_AT_TOP_LEVEL) return;
    SourceFile const* file = at.source;
    CompilerOptions const* options = compiler_options();
    if (!file->is_module) {
        error_ctx_grammar_error_d(errs, at, kind == AWAIT_KIND_EXPR ? 1375 : kind == AWAIT_KIND_FOR ? 1431 : 2853,
            what[kind], " are only allowed at the top level of a file when that file is a module, but this file has "
            "no imports or exports. Consider adding an empty 'export {}' to make this file a module.");
    }
    bool node = options->module == MODULE_KIND_NODE16 || options->module == MODULE_KIND_NODE18 ||
        options->module == MODULE_KIND_NODENEXT;
    bool es = options->module == MODULE_KIND_ES2022 || options->module == MODULE_KIND_ESNEXT ||
        options->module == MODULE_KIND_PRESERVE || options->module == MODULE_KIND_SYSTEM;
    if (node && source_file_implied_format_for_emit(file) == MODULE_KIND_COMMONJS) {
        error_ctx_grammar_error_d(errs, at, 1309, "The current file is a CommonJS module and cannot use 'await' at the top level.");
    } else if (!(node || es) || options->target < SCRIPT_TARGET_ES2017) {
        error_ctx_grammar_error_d(errs, at, kind == AWAIT_KIND_EXPR ? 1378 : kind == AWAIT_KIND_FOR ? 1432 : 2854,
            "Top-level ", what[kind], " are only allowed when the 'module' option is set to 'es2022', 'esnext', 'system', "
            "'node16', 'node18', 'nodenext', or 'preserve', and the 'target' option is set to 'es2017' or higher.");
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

// tsc's getJsxNamespace: the name the factory of an element (or fragment) starts with. A fragment goes by its own
// factory (pragma, then option) else the program's, not by an `@jsx` pragma. Invalid names count as not given
static csview jsx_factory_name(SourceFile const* file, bool fragment) {
    CompilerOptions const* options = compiler_options();
    if (fragment) {
        csview pragma = file ? file->jsx_pragmas.fragment_factory : c_sv("");
        if (compiler_option_is_entity_name(pragma)) return first_identifier(pragma);
        if (compiler_option_is_entity_name(cstr_sv(&options->jsx_fragment_factory))) {
            return first_identifier(cstr_sv(&options->jsx_fragment_factory));
        }
    } else if (file && compiler_option_is_entity_name(file->jsx_pragmas.factory)) {
        return first_identifier(file->jsx_pragmas.factory);
    }
    if (cstr_size(&options->jsx_factory)) {
        csview factory = cstr_sv(&options->jsx_factory);
        return compiler_option_is_entity_name(factory) ? first_identifier(factory) : c_sv("React");
    }
    if (cstr_size(&options->react_namespace)) return cstr_sv(&options->react_namespace);
    return c_sv("React");
}

// tsc's getJsxNamespaceContainerForImplicitImport: the module the file's elements import their factory from, null if
// there is none. When it can't be found, the first element of the file says so
static Symbol* jsx_runtime_module(Span at, Scope* names, ErrorCtx* errs) {
    ModuleRef* ref = names->file ? names->file->jsx_runtime_ref : nullptr;
    if (!ref) return nullptr;
    Symbol* module = binder_module_of_ref(names->binder, ref);
    if (module || ref->jsx_runtime_reported) return module;
    ref->jsx_runtime_reported = true;
    if (!ref->ambient_only && ref->failure != MODULE_REF_NOT_FOUND) return nullptr; // TODO: TS7016 for a JavaScript module
    if (compiler_options()->module_resolution == MODULE_RESOLUTION_CLASSIC) {
        error_ctx_error_d(errs, at, 2792, "Cannot find module '", ref->path, "'. Did you mean to set the "
            "'moduleResolution' option to 'nodenext', or to add aliases to the 'paths' option?");
    } else {
        error_ctx_error_d(errs, at, 2875, "This JSX tag requires the module path '", ref->path, "' to exist, but none "
            "could be found. Make sure you have types for the appropriate package installed.");
    }
    return nullptr;
}

static Symbol* jsx_reference_factory(JsxElement const* el, bool fragment, csview name, Scope* names, ErrorCtx* errs) {
    JsxKind jsx = compiler_options()->jsx;
    u32 meaning = jsx == JSX_PRESERVE || jsx == JSX_REACT_NATIVE ? SYMBOL_VALUE & ~(u32)SYMBOL_ENUM : SYMBOL_VALUE;
    Symbol* found = scope_resolve(names, name, meaning);
    if (found) {
        unused_mark_referenced(names, found, SYMBOL_VALUE);
    } else if (jsx == JSX_REACT) {
        Span at = fragment || !el->tag_name.inner_span.sv.size ? el->opening_span : el->tag_name.inner_span;
        // (tsc's resolveName: a misspelling is said to be one instead)
        if (!scope_report_misspelled(names, at, name, meaning, errs)) {
            error_ctx_error_d(errs, at, 2874, "This JSX tag requires '", name, "' to be in scope, but it could not be found.");
        }
    }
    return found;
}

// tsc's markJsxAliasReferenced (and the namespace half of getJsxNamespaceAt): every element refers to its factory
static void jsx_lookup_factory(Expr* expr, Scope* names, ErrorCtx* errs) {
    JsxElement* el = expr->jsx;
    bool fragment = expr->tag == EXPR_JSX_FRAGMENT;
    csview name = jsx_factory_name(names->file, fragment);
    el->factory_namespace = scope_resolve(names, name, SYMBOL_NAMESPACE);
    // With a runtime module the factory isn't referenced at all (tsc's markJsxAliasReferenced)
    // (Blamed on the element, only the opening `<>` of a fragment: whichever tsc asks at first)
    el->runtime_module = jsx_runtime_module(fragment ? el->opening_span : expr->inner_span, names, errs);
    CompilerOptions const* options = compiler_options();
    SourceFile const* file = names->file;
    if (fragment && options->jsx != JSX_PRESERVE && options->jsx != JSX_REACT_NATIVE && options->jsx != JSX_NONE &&
        !cstr_size(&options->jsx_fragment_factory) && !(file && file->jsx_pragmas.fragment_factory.size)
    ) {
        // A factory of its own needs a fragment factory of its own (tsc's checkJsxFragment)
        if (cstr_size(&options->jsx_factory)) {
            error_ctx_error_d(errs, expr->inner_span, 17016, "The 'jsxFragmentFactory' compiler option must be provided "
                "to use JSX fragments with the 'jsxFactory' compiler option.");
        } else if (file && file->jsx_pragmas.factory.size) {
            error_ctx_error_d(errs, expr->inner_span, 17017,
                "An @jsxFrag pragma is required when using an @jsx pragma with JSX fragments.");
        }
    }
    if (el->runtime_module) return;
    // `null` as the fragment factory (tsc allows it)
    bool null_fragment = fragment && csview_equals(name, "null");
    if (!null_fragment) el->factory_value = jsx_reference_factory(el, fragment, name, names, errs);
    // A fragment refers to the element factory too
    if (fragment) jsx_reference_factory(el, fragment, jsx_factory_name(file, false), names, errs);
    // What a fragment evaluates to is looked up once per file (tsc's getJSXFragmentType), and said to be missing then
    if (fragment && !null_fragment && file && !file->jsx_fragment_factory_looked_up &&
        (options->jsx == JSX_REACT || cstr_size(&options->jsx_fragment_factory))
    ) {
        ((SourceFile*)file)->jsx_fragment_factory_looked_up = true; // (the resolve walk's only per-file state)
        if (!el->factory_value) {
            error_ctx_error_d(errs, el->opening_span, 2879, "Using JSX fragments requires fragment factory '", name,
                "' to be in scope, but it could not be found.");
        }
    }
}

// tsc's checkGrammarJsxElement for the attributes: the first repeated name (TS17001) or empty `x={}` (TS17000)
static void jsx_check_attributes_grammar(JsxElement const* el, ErrorCtx* errs) {
    VecJsxAttribute const* attributes = &el->attributes.jsx_attributes;
    for (isize i = 0; i < vec_jsx_attribute_size(attributes); i++) {
        JsxAttribute const* attr = vec_jsx_attribute_at(attributes, i);
        if (attr->tag == JSX_ATTRIBUTE_SPREAD) continue;
        for (isize j = 0; j < i; j++) {
            JsxAttribute const* earlier = vec_jsx_attribute_at(attributes, j);
            if (earlier->tag != JSX_ATTRIBUTE_SPREAD && csview_equals_sv(earlier->name.sv, attr->name.sv)) {
                error_ctx_grammar_error_d(errs, attr->name, 17001,
                    "JSX elements cannot have multiple attributes with the same name.");
                return;
            }
        }
        if (attr->tag == JSX_ATTRIBUTE_EMPTY_EXPR) {
            error_ctx_grammar_error_d(errs, attr->value.inner_span, 17000,
                "JSX attributes must only be assigned a non-empty 'expression'.");
            return;
        }
    }
}

// Not inlined, like jsx_typecheck()
__attribute__((noinline)) static void jsx_lookup_names(Expr* me, Scope* names, ErrorCtx* errs) {
    switch (my tag) {
        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT: {
            jsx_lookup_factory(me, names, errs);
            if (my tag == EXPR_JSX_ELEMENT) jsx_check_attributes_grammar(my jsx, errs);
            // A value tag (`<Comp />`, `<this.x />`) is a reference, an intrinsic one (`<div />`) is a name
            // (a missing name, after a parse error, is nothing)
            bool intrinsic = my tag == EXPR_JSX_FRAGMENT || jsx_element_is_intrinsic(my jsx) ||
                !my jsx->tag_name.inner_span.sv.size;
            if (!intrinsic) {
                expr_lookup_names(&my jsx->tag_name, names, errs);
            } else if (my tag == EXPR_JSX_ELEMENT && !my jsx->namespaced) {
                // Only for the types baseline, which shows an intrinsic tag name as a value of that name (tsc's
                // getTypeAtLocation checks it as an expression): no reference
                IdentifierExpr* name = &my jsx->tag_name.identifier;
                if (!name->keyword) name->target = scope_get_value(names, name->name.sv, &name->symbol);
                IdentifierExpr* closing = &my jsx->closing_name.identifier;
                if (my jsx->closing_name.tag == EXPR_IDENTIFIER && !closing->keyword) {
                    closing->target = scope_get_value(names, closing->name.sv, &closing->symbol);
                }
            }
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

static bool is_plain_binary(Expr const* me) {
    if (my tag != EXPR_BINARY_OP) return false;
    BinaryOp op = my binary_op->op;
    return !binary_op_is_logical(op) && !binary_op_is_logical_assignment(op) && !binary_op_is_assignment(op);
}

// expr_lookup_names() of a binary expression that isn't logical or an assignment, once its left operand is walked
static void plain_binary_lookup_names_rest(Expr* me, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    if (my binary_op->op == BINARY_OP_COMMA) flow_maybe_call(flow, &my binary_op->left);
    expr_lookup_names(&my binary_op->right, names, errs);
    if (my binary_op->op == BINARY_OP_COMMA) flow_maybe_call(flow, &my binary_op->right);
}

// A chain of them (`a + b + c + ...`, `a, b, c, ...`) is walked down its left spine with a loop, not a recursion per
// operand: generated code has chains long enough to overflow the stack. The operands of these start from a clean flow
// state (nothing for expr_lookup_names() to take), so each level only needs its lazy type bound on the way down
static void plain_binary_lookup_names(Expr* me, Scope* names, ErrorCtx* errs) {
    u32 depth = 0;
    for (Expr* e = me; is_plain_binary(&e->binary_op->left); e = &e->binary_op->left) depth++;
    Expr* local[16];
    Expr** spine = depth <= c_arraylen(local) ? local : malloc(sizeof(Expr*) * depth);
    panic_if(!spine, "out of memory");
    Expr* e = me;
    for (u32 i = 0; i < depth; i++, e = &e->binary_op->left) {
        spine[i] = e;
        if (i) lazy_type_bind(&e->lazy_type, LAZY_TYPE_EXPR);
    }
    if (depth) {
        expr_lookup_names(e, names, errs); // The bottom one, whose left operand isn't one
    } else {
        expr_lookup_names(&my binary_op->left, names, errs);
        spine[depth++] = me;
    }
    for (u32 i = depth; i-- > 0;) plain_binary_lookup_names_rest(spine[i], names, errs);
    if (spine != local) free(spine);
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
            if (my binary_op->jsdoc_type) type_lookup_names(my binary_op->jsdoc_type, names, errs);
            // `o.m = function () {}` (or `??=` & co.): `this` in there may be `o`
            if (binary_op_is_assignment(op) && right->tag == EXPR_FUNCTION && !span_valid(right->function->arrow_span) &&
                left->tag == EXPR_PROPERTY
            ) {
                right->function->this_assigned_object = &left->property->expr;
            }
            if (op == BINARY_OP_EQUAL && left->tag == EXPR_PROPERTY && left->inner_span.source && left->inner_span.source->is_js) {
                Expr const* constructor = expr_prototype_object(&left->property->expr);
                if (constructor && right->tag == EXPR_FUNCTION) right->function->prototype_of = constructor;
                constructor = expr_prototype_object(left);
                if (constructor && right->tag == EXPR_OBJECT) {
                    for_each (i, vec_object_expr_item, &right->object) {
                        if (i->value.tag == EXPR_FUNCTION) i->value.function->prototype_of = constructor;
                    }
                }
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
                if (op == BINARY_OP_EQUAL && names->file && names->file->is_js) scope_add_this_property(names, my binary_op);
                if (op == BINARY_OP_EQUAL && names->file && !names->file->is_js) scope_note_ts_this_assignment(names, my binary_op);
                expr_lookup_names(right, names, errs);
                if (!assignment_target) {
                    flow_assignment_target(flow, left, (FlowAssign){ .kind = FLOW_ASSIGN_EXPR, .assignment = me });
                }
            } else {
                plain_binary_lookup_names(me, names, errs);
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

                case UNARY_OP_AWAIT:
                    // tsc's checkAwaitGrammar: reported whether or not the file has parse errors
                    if (my unary_op->await_place == AWAIT_IN_STATIC_BLOCK) {
                        error_ctx_error_d(errs, my inner_span, 18037, "'await' expression cannot be used inside a class static block.");
                    }
                    await_check_grammar(my unary_op->await_place, AWAIT_KIND_EXPR,
                        (Span){ .source = my inner_span.source, .sv = csview_from_n(my inner_span.sv.buf, 5) }, names->function, errs);
                    expr_lookup_names(operand, names, errs);
                    break;

                case UNARY_OP_YIELD:
                case UNARY_OP_YIELD_STAR:
                    if (my unary_op->outside_generator) {
                        // tsc's checkYieldExpressionGrammar; checkYieldExpression returns any before the operand
                        Span keyword = { .source = my inner_span.source, .sv = csview_from_n(my inner_span.sv.buf, 5) };
                        error_ctx_grammar_error_d(errs, keyword, 1163, "A 'yield' expression is only allowed in a generator body.");
                        expr_lookup_names(operand, names, nullptr);
                        break;
                    }
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
            if (my property->form == PROPERTY_EXPR_DOT || my property->prop.tag == EXPR_LITERAL) {
                if (my property->expr.tag == EXPR_IDENTIFIER) my property->expr.identifier.constraint_position = true;
                if (my property->expr.tag == EXPR_PROPERTY) my property->expr.property->constraint_position = true;
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
            if (callee->tag == EXPR_PROPERTY) callee->property->constraint_position = true;
            if (callee->tag == EXPR_PROPERTY) callee->property->method_call = true;
            if (callee->tag == EXPR_FUNCTION && !my call->uses_new) {
                // An immediately invoked function expression is part of this flow, after its arguments
                for_each (i, vec_expr, &my call->args) {
                    expr_lookup_names(i, names, errs);
                }
                flow->next_function_is_iife = !callee->function->is_async && !callee->function->is_generator;
                callee->function->is_iife = true;
                callee->function->iife_arg_count = (u32)vec_expr_size(&my call->args);
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
                name_scope->expression_name_of = my function;
                function_impl_add_names(my function, name_scope, DECL_EXPORTED_NO, false, false);
            }
            my function->syntactically_contextual = flow->contextual_depth > 0;
            function_impl_lookup_names_as(my function, FUNCTION_IS_EXPRESSION, !span_valid(my function->arrow_span), name_scope, errs);
        }   break;

        case EXPR_OBJECT:
            my assignment_pattern = assignment_target != FLOW_TARGET_NONE;
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
                if (i->jsdoc_type) type_lookup_names(i->jsdoc_type, names, errs);
                if (i->shorthand_default && assignment_target == FLOW_TARGET_NONE && i->value.tag == EXPR_IDENTIFIER) {
                    i->value.identifier.shorthand_unchecked = true;
                }
                expr_lookup_names(&i->value, names, errs);
                if (i->shorthand_default) expr_lookup_names_initializer(i->shorthand_default, names, errs);
            }
            break;

        case EXPR_ARRAY:
            my assignment_pattern = assignment_target != FLOW_TARGET_NONE;
            for_each (i, vec_expr, &my array) {
                flow->assignment_target = assignment_target;
                expr_lookup_names(i, names, errs);
            }
            break;

        case EXPR_CLASS_DECL: {
            Scope* class_name_scope = scope_new(names, SCOPE_BLOCK);
            class_decl_add_names(my class_decl, class_name_scope, DECL_EXPORTED_NO, false);
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
static void assignment_target_typecheck(Expr const* target, Ty const* source, Expr const* source_expr, ErrorCtx* errs);

void expr_destructuring_typecheck(Expr const* target, Ty const* source, ErrorCtx* errs) {
    assignment_target_typecheck(target, source, nullptr, errs);
}

// JavaScript `ns.a = {}` where `ns.a.b = v` makes `ns.a` a namespace: an assignment declaration that isn't checked
// against what it declares, which is necessarily incomplete (tsc's isAssignmentDeclaration in checkBinaryLikeExpression)
static bool is_container_declaration(BinaryOpExpr const* op) {
    SourceFile const* file = op->left.inner_span.source;
    if (op->op != BINARY_OP_EQUAL || !file || !file->is_js || op->left.tag != EXPR_PROPERTY) return false;
    Expr const* init = expando_initializer((Expr*)&op->right, span_null, true);
    if (!init || init->tag != EXPR_OBJECT) return false;
    // (Only if it does declare members: `symbol.exports.size`. Unknown for CommonJS exports, taken as yes)
    Symbol const* declared = op->left.property->declares;
    return !declared || (declared->exports && declared->exports->count);
}

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
            } else if (source && target->identifier.is_arguments) {
                // `arguments = x` in a function: the arguments object (IArguments)
                ty_check_assignable_expr(checker, source, ty_expr_type(checker, target), target->outer_span, source_expr, errs);
            }
            return;

        case EXPR_PROPERTY: {
            if (!source) return;
            Ty const* target_type = ty_expr_type(checker, target);
            if (target->property->assignment_target == FLOW_TARGET_COMPOUND) {
                target_type = ty_compound_assignee_type(checker, target, target_type);
            }
            ty_check_assignable_to_access(checker, source, target_type, target->outer_span, source_expr, target, errs);
        }   return;

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
            bool array_like = source && !(source->flags & TYF_NEVER) && ty_extends_array(checker, source);
            // What isn't an array has to be iterable (tsc's checkArrayLiteralAssignment)
            Ty const* iterated = source && !array_like ?
                ty_iterated_type(checker, ITERATION_DESTRUCTURING, source, target->outer_span, errs) : nullptr;
            u32 index = 0;
            for_each (i, vec_expr, &target->array) {
                Ty const* element = iterated;
                if (i->tag == EXPR_SPREAD) {
                    // The rest of a tuple, else an array of the elements (tsc's checkArrayLiteralDestructuringElementAssignment)
                    Ty const* rest = nullptr;
                    if (source) {
                        rest = ty_tuple_rest_slice(checker, source, index);
                        if (!rest) {
                            if (array_like) element = ty_access(checker, source, ty_intrinsic(checker, TY_NUMBER));
                            rest = element ? ty_array(checker, element, false) : nullptr;
                        }
                    }
                    assignment_target_typecheck(i->spread, rest, nullptr, errs);
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
                    Ty const* key = ty_string_literal(checker, cstr_sv(&i->name_string));
                    property = ty_access(checker, source, key);
                    ty_mark_destructured_member(checker, source, cstr_sv(&i->name_string));
                    // What the source lacks is TS2339, on the source as it is: null and undefined too (tsc's
                    // checkObjectLiteralDestructuringPropertyAssignment). An object literal source may lack a property
                    // that has a default (AccessFlags.AllowMissing)
                    bool has_default = i->shorthand_default ||
                        (i->value.tag == EXPR_BINARY_OP && i->value.binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(&i->value));
                    if (!property && !(has_default && source->kind == TY_ANONYMOUS && source->anonymous.object_literal)) {
                        ty_check_property_access(checker, source, key, span_null, i->name_span, false, true, errs);
                    }
                }
                if (i->tag == OBJECT_ITEM_SPREAD) {
                    assignment_target_typecheck(&i->value, nullptr, nullptr, errs); // TODO: the rest of the object
                } else if (i->tag == OBJECT_ITEM_SHORTHAND && i->shorthand_default) {
                    // `{ a = 1 } = o`: the default is assigned to `a` (checked with a's type as its contextual type),
                    // and so is the property, without undefined unless the default can be undefined too (tsc's
                    // checkDestructuringAssignment of a ShorthandPropertyAssignment)
                    Ty const* declared = ty_expr_type(checker, &i->value);
                    Ty const* default_type = ty_check_expr(checker, i->shorthand_default, declared);
                    expr_typecheck(i->shorthand_default, errs);
                    assignment_target_typecheck(&i->value, default_type, i->shorthand_default, errs);
                    if (property && compiler_options()->strict_null_checks &&
                        !(default_type->flags & (TYF_ANY | TYF_UNKNOWN)) &&
                        ty_without_undefined(checker, default_type) == default_type
                    ) {
                        property = ty_without_undefined(checker, property);
                    }
                    assignment_target_typecheck(&i->value, property, nullptr, errs);
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
// tsc's checkGrammarImportCallExpression (the trailing comma part aside)
static void import_call_check_grammar(Expr const* me, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    ModuleKind module = options->module;
    if (options->verbatim_module_syntax && module == MODULE_KIND_COMMONJS) {
        error_ctx_grammar_error_d(errs, my inner_span, 1286,
            "ESM syntax is not allowed in a CommonJS module when 'verbatimModuleSyntax' is enabled.");
        return;
    }
    if (module == MODULE_KIND_ES2015) {
        error_ctx_grammar_error_d(errs, my inner_span, 1323,
            "Dynamic imports are only supported when the '--module' flag is set to 'es2020', 'es2022', 'esnext', "
            "'commonjs', 'amd', 'system', 'umd', 'node16', 'node18', or 'nodenext'.");
        return;
    }
    VecExpr const* args = &my call->args;
    isize count = vec_expr_size(args);
    bool node = module >= MODULE_KIND_NODE16 && module <= MODULE_KIND_NODENEXT;
    if (!node && module != MODULE_KIND_ESNEXT && module != MODULE_KIND_PRESERVE && count > 1) {
        error_ctx_grammar_error_d(errs, vec_expr_at(args, 1)->outer_span, 1324,
            "Dynamic imports only support a second argument when the '--module' option is set to 'esnext', 'node16', "
            "'node18', 'nodenext', or 'preserve'.");
        return;
    }
    if (count == 0 || count > 2) {
        error_ctx_grammar_error_d(errs, my inner_span, 1450,
            "Dynamic imports can only accept a module specifier and an optional set of attributes as arguments");
        return;
    }
    for_each (i, vec_expr, args) {
        if (i->tag != EXPR_SPREAD) continue;
        error_ctx_grammar_error_d(errs, i->outer_span, 1325, "Argument of dynamic import cannot be spread element.");
        return;
    }
}

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

bool expr_is_non_bindable_dynamic_name(Expr const* key) {
    if (!expr_is_perenthisized(key)) {
        Expr const* literal = key;
        if (key->tag == EXPR_UNARY_OP && (key->unary_op->op == UNARY_OP_PLUS || key->unary_op->op == UNARY_OP_MINUS) &&
            key->unary_op->expr.tag == EXPR_LITERAL && key->unary_op->expr.literal.tag == VALUE_NUMBER &&
            !expr_is_perenthisized(&key->unary_op->expr)
        ) {
            literal = &key->unary_op->expr;
        }
        if (literal->tag == EXPR_LITERAL && (literal->literal.tag == VALUE_STRING || literal->literal.tag == VALUE_NUMBER)) {
            return false;
        }
    }
    return !expr_is_entity_name(key);
}

// tsc's getEffectivePropertyNameForPropertyNameNode: a computed key is named by its literal or unique symbol type, and
// `[+1]` by its operand. Null: no name the grammar check can compare
static Atom const* object_item_effective_name(ObjectExprItem const* item) {
    if (!item->computed_key) return checker_atom(current_checker, cstr_sv(&item->name_string));
    Expr const* key = item->computed_key;
    if (key->tag == EXPR_UNARY_OP && key->unary_op->op == UNARY_OP_PLUS && key->unary_op->expr.tag == EXPR_LITERAL &&
        key->unary_op->expr.literal.tag == VALUE_NUMBER
    ) {
        key = &key->unary_op->expr;
    }
    bool is_number = false;
    return ty_property_name_of_key(current_checker, ty_expr_type(current_checker, key), &is_number);
}

// tsc's checkGrammarObjectLiteralExpression: a name can be one property, one method or one get/set pair
static void object_check_duplicate_names(Expr const* me, ErrorCtx* errs) {
    if (!my outer_span.source || my outer_span.source->has_parse_errors || is_destructuring_target(me)) return;
    enum { GET = 1, SET = 2, PROPERTY = 4, METHOD = 8, ACCESSOR = GET | SET };
    ObjectExpr const* object = &my object;
    isize count = vec_object_expr_item_size(object);
    uint8_t* seen = calloc(count ? (size_t)count : 1, 1);
    Atom const** names = calloc(count ? (size_t)count : 1, sizeof(Atom const*));
    panic_if(!seen || !names, "out of memory");
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
        names[n] = object_item_effective_name(item);
        if (!names[n]) continue;
        isize existing = -1;
        for (isize j = 0; j < n && existing < 0; j++) {
            if (seen[j] && names[j] == names[n]) existing = j;
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
    free(names);
}

// tsc's binder declares the members of an object literal like any symbols: properties and methods exclude nothing, an
// accessor excludes every other value but its other half. A conflict is TS2300 on every declaration of the name so far
// and on the new one, which then starts over (this is not a grammar check: also with parse errors)
static void object_bind_members(Expr const* me, ErrorCtx* errs) {
    enum { PROPERTY = 1, METHOD = 2, GET = 4, SET = 8, VALUE = PROPERTY | METHOD | GET | SET };
    ObjectExpr const* object = &my object;
    isize count = vec_object_expr_item_size(object);
    // Per item: the flags of the symbol it was declared into, and the item that symbol started with
    uint8_t* flags = calloc(count ? (size_t)count : 1, 1);
    isize* start = malloc(sizeof(isize) * (count ? (size_t)count : 1));
    panic_if(!flags || !start, "out of memory");
    for (isize n = 0; n < count; n++) {
        ObjectExprItem const* item = vec_object_expr_item_at(object, n);
        uint8_t includes, excludes;
        switch (item->tag) {
            case OBJECT_ITEM_PROPERTY: case OBJECT_ITEM_SHORTHAND: includes = PROPERTY; excludes = 0; break;
            case OBJECT_ITEM_METHOD: includes = METHOD; excludes = 0; break;
            case OBJECT_ITEM_GETTER: includes = GET; excludes = VALUE & ~SET; break;
            case OBJECT_ITEM_SETTER: includes = SET; excludes = VALUE & ~GET; break;
            default: start[n] = -1; continue;
        }
        start[n] = n;
        if (item->computed_key || !span_valid(item->name_span)) {
            start[n] = -1;
            continue;
        }
        // The symbol of that name: the latest item declared under it
        isize last = -1;
        for (isize j = n - 1; j >= 0 && last < 0; j--) {
            ObjectExprItem const* other = vec_object_expr_item_at(object, j);
            if (start[j] >= 0 && cstr_equals_sv(&other->name_string, cstr_sv(&item->name_string))) last = j;
        }
        if (last < 0) {
            flags[n] = includes;
            continue;
        }
        if (!(flags[last] & excludes)) {
            flags[n] = flags[last] | includes;
            start[n] = start[last];
            continue;
        }
        for (isize j = start[last]; j < n; j++) {
            ObjectExprItem const* other = vec_object_expr_item_at(object, j);
            if (start[j] != start[last]) continue;
            Span at = other->name_span;
            if (!error_ctx_find(errs, at, 2300)) error_ctx_error_d(errs, at, 2300, "Duplicate identifier '", at, "'.");
        }
        error_ctx_error_d(errs, item->name_span, 2300, "Duplicate identifier '", item->name_span, "'.");
        flags[n] = includes;
    }
    free(flags);
    free(start);
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
    if (operand->tag == EXPR_IMPLICIT_ANY) return; // A missing operand (tsc's missing identifier is fine)
    if (is_written_undefined(operand) && !expr_is_perenthisized(operand)) {
        // tsc's undefinedSymbol: its type isn't reported as possibly undefined, but it's no variable
        error_ctx_error_d(errs, operand->inner_span, 2539, "Cannot assign to 'undefined' because it is not a variable.");
        return;
    }
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
// An object made where it is compared (tsc's isLiteralExpressionOfObject): not parenthesized, arrow functions aren't
static bool is_literal_expression_of_object(Expr const* me) {
    if (!span_eq(my inner_span, my outer_span)) return false;
    switch (my tag) {
        case EXPR_OBJECT: case EXPR_ARRAY: case EXPR_REGEX: case EXPR_CLASS_DECL: return true;
        case EXPR_FUNCTION: return !span_valid(my function->arrow_span);
        default: return false;
    }
}


// Returns whether a compound assignment is checked too: not after its operands were rejected (tsc's
// checkBinaryLikeExpression, whose checkAssignmentOperator is skipped then)
static bool binary_op_typecheck(
    Ty const* left,
    BinaryOp op,
    Ty const* right,
    Expr const* expr,
    ErrorCtx* errs
) {
    bool operands_ok = true;
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
                if (!left || !right) return false;
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
            operands_ok = false;
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
                operands_ok = false;
                break;
            }
            Ty const* numeric_types[] = { ty_intrinsic(checker, TY_NUMBER), ty_intrinsic(checker, TY_BIGINT) };
            Ty const* number_or_bigint = ty_union(checker, numeric_types, 2, nullptr, (TyList){});
            if (!ty_is_related(checker, left, number_or_bigint, TY_RELATION_ASSIGNABLE)) {
                error_ctx_error_d(errs, expr->binary_op->left.outer_span, 2362,
                    "The left-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type.");
                operands_ok = false;
            }
            if (!ty_is_related(checker, right, number_or_bigint, TY_RELATION_ASSIGNABLE)) {
                error_ctx_error_d(errs, expr->binary_op->right.outer_span, 2363,
                    "The right-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type.");
                operands_ok = false;
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
                operands_ok = false;
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
            if (is_container_declaration(expr->binary_op)) break;
            // Nor is `module.exports = v` of an object type, which the module's other exports add to
            if (right && (right->flags & TYF_OBJECT) &&
                binary_commonjs_declaration(expr->binary_op, nullptr) == COMMONJS_TARGET_MODULE_EXPORTS
            ) {
                break;
            }
            assignment_target_typecheck(&expr->binary_op->left, right, &expr->binary_op->right, errs);
            break;

        case BINARY_OP_DOUBLE_EQUAL:
        case BINARY_OP_TRIPLE_EQUAL:
        case BINARY_OP_NOT_EQUAL:
        case BINARY_OP_TRIPLE_NOT_EQUAL: {
            // A fresh object is equal to nothing else (only `===` and `!==` in JS)
            bool strict_op = op == BINARY_OP_TRIPLE_EQUAL || op == BINARY_OP_TRIPLE_NOT_EQUAL;
            Expr const* l = &expr->binary_op->left;
            if ((is_literal_expression_of_object(l) || is_literal_expression_of_object(&expr->binary_op->right)) &&
                (!l->inner_span.source || !l->inner_span.source->is_js || strict_op)
            ) {
                bool eq = op == BINARY_OP_DOUBLE_EQUAL || op == BINARY_OP_TRIPLE_EQUAL;
                error_ctx_error_d(errs, expr->outer_span, 2839, "This condition will always return '", eq ? "false" : "true",
                    "' since JavaScript compares objects by reference, not value.");
            }
        }
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
            // The expression's own type is what gets assigned
            if (binary_op_typecheck(left, without_assignment, right, expr, errs)) {
                binary_op_typecheck(left, BINARY_OP_EQUAL, expr_get_type((Expr*)expr), expr, errs);
            }
        }   break;

        case BINARY_OP_COMMA:
        case BINARY_OP_EH_EH:
            break;
    }
    return operands_ok;
}

Ty const* expr_evaluate_type(Expr const* me) {
    // Nobody checked this expression with a contextual type first, see ty/expr.c
    return ty_compute_expr_without_context(current_checker, me);
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

        case EXPR_JSX_ATTRIBUTES: {
            TyWrittenProp* written = nullptr;
            u32 written_count = 0;
            for_each (i, vec_jsx_attribute, &my jsx_attributes) {
                if (i->tag != JSX_ATTRIBUTE_NO_VALUE && i->tag != JSX_ATTRIBUTE_EMPTY_EXPR) {
                    expr_typecheck(&i->value, errs);
                }
                if (i->tag == JSX_ATTRIBUTE_SPREAD) {
                    Ty const* type = expr_get_type(&i->value);
                    ty_check_spread_type(current_checker, type, i->value.outer_span, errs);
                    ty_check_spread_overrides(current_checker, type, written, written_count, i->span, errs);
                } else {
                    written = realloc(written, sizeof(TyWrittenProp) * (written_count + 1));
                    panic_if(!written, "out of memory");
                    written[written_count++] = (TyWrittenProp){ checker_atom(current_checker, i->name.sv), i->span };
                }
            }
            free(written);
        }   break;

        default: impossible;
    }
}

// The binary case of expr_typecheck() once the left operand is checked
static void binary_expr_typecheck_rest(Expr const* me, ErrorCtx* errs) {
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
}

// The binary case of expr_typecheck(). A chain (`a + b + c + ...`) is walked down its left spine with a loop, not a
// recursion per operand: generated code has chains long enough to overflow the stack (tsc checks them iteratively too)
static void binary_expr_typecheck(Expr const* me, ErrorCtx* errs) {
    if (my binary_op->jsdoc_type) type_typecheck(my binary_op->jsdoc_type, errs);
    if (my binary_op->op == BINARY_OP_IN && my binary_op->left.tag == EXPR_PRIVATE_NAME &&
        !expr_is_perenthisized(&my binary_op->left)
    ) {
        // The one place `#x` is an expression
        if (private_names_lowered()) {
            ty_check_emit_helpers(current_checker, my binary_op->left.outer_span, EMIT_HELPER_CLASS_PRIVATE_FIELD_IN, errs);
        }
        expr_typecheck(&my binary_op->right, errs);
        ty_check_in_expression(current_checker, &my binary_op->left, &my binary_op->right, errs);
        return;
    }
    u32 depth = 0;
    for (Expr const* e = me; e->binary_op->left.tag == EXPR_BINARY_OP; e = &e->binary_op->left) depth++;
    if (!depth) {
        expr_typecheck(&my binary_op->left, errs);
        binary_expr_typecheck_rest(me, errs);
        return;
    }
    // In the order the recursion would: each level's own type on the way down, the rest on the way up
    Expr const* local[16];
    Expr const** spine = depth <= c_arraylen(local) ? local : malloc(sizeof(Expr const*) * depth);
    panic_if(!spine, "out of memory");
    Expr const* e = me;
    for (u32 i = 0; i < depth; i++, e = &e->binary_op->left) {
        spine[i] = e;
        if (i) expr_get_type((Expr*)e);
    }
    expr_typecheck(e, errs); // The bottom binary expression, whose left operand isn't one
    for (u32 i = depth; i-- > 0;) binary_expr_typecheck_rest(spine[i], errs);
    if (spine != local) free(spine);
}

// `import.meta` needs a module format that has it (tsc's checkImportMetaProperty)
static void import_meta_typecheck(Expr const* me, ErrorCtx* errs) {
    ModuleKind module = compiler_options()->module;
    if (module >= MODULE_KIND_NODE16 && module <= MODULE_KIND_NODENEXT) {
        if (source_file_implied_format_for_emit(my inner_span.source) != MODULE_KIND_ESNEXT) {
            error_ctx_error_d(errs, my inner_span, 1470,
                "The 'import.meta' meta-property is not allowed in files which will build into CommonJS output.");
        }
    } else if (module < MODULE_KIND_ES2020 && module != MODULE_KIND_SYSTEM) {
        error_ctx_error_d(errs, my inner_span, 1343, "The 'import.meta' meta-property is only allowed when the "
            "'--module' option is 'es2020', 'es2022', 'esnext', 'system', 'node16', 'node18', or 'nodenext'.");
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

        case EXPR_BINARY_OP: binary_expr_typecheck(me, errs); break;

        case EXPR_UNARY_OP:
            if (my unary_op->outside_generator) break; // Only TS1163 (from the resolve walk)
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
                    // `+x` converts to a number, which throws for a bigint
                    if (my unary_op->op == UNARY_OP_PLUS && maybe_kind(operand, TYF_BIGINT | TYF_BIGINT_LITERAL, true)) {
                        error_ctx_error_d(errs, expr_error_span(&my unary_op->expr), 2736, "Operator '+' cannot be applied to type '",
                            ty_base_type_of_literal(current_checker, operand), "'.");
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
            if (ty_is_import_meta(me)) import_meta_typecheck(me, errs);
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
            // `null.x`, `undefined[0]`, `u.x` with `u: undefined` (tsc's checkNonNullExpression; a union with null
            // or undefined is check_access()'s)
            if ((expr_type->flags & TYF_NULLABLE) && expr_type->kind != TY_UNION) {
                ty_check_non_null_type(current_checker, expr_type, &my property->expr, errs);
                break;
            }
            if (my property->qualified_name && my property->prop.tag == EXPR_LITERAL &&
                ty_namespace_type_only_prop(current_checker, expr_type, ty_literal_name(current_checker, ty_access_key_type(current_checker, my property)))
            ) {
                break;
            }
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
            ty_check_element_implicit_any(current_checker, my property, expr_type, my inner_span, errs);
            ty_check_generic_element_access(current_checker, my property, expr_type, my inner_span, errs);
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
                    my as_type->type.from_jsdoc ? my as_type->keyword_span : my inner_span, errs);
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
            // `import("m", options)`: the options are ImportCallOptions
            if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT &&
                vec_expr_size(&my call->args) > 1
            ) {
                ty_check_import_call_options(checker, vec_expr_at(&my call->args, 1), errs);
            }
            ty_call_typecheck(checker, callee_type, my call, contextual, my outer_span, errs);
            if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT) {
                import_call_check_grammar(me, errs);
            }
            for_each (i, vec_expr, &my call->args) {
                expr_typecheck(i, errs);
            }
        }   break;

        case EXPR_FUNCTION:
            function_impl_typecheck(my function, errs);
            break;

        case EXPR_OBJECT: {
            object_bind_members(me, errs);
            object_check_duplicate_names(me, errs);
            ty_check_object_literal_pattern(current_checker, me, errs);
            TyWrittenProp* written = nullptr;
            u32 written_count = 0;
            for_each (i, vec_object_expr_item, &my object) {
                if (i->tag != OBJECT_ITEM_SPREAD && !i->computed_key) {
                    written = realloc(written, sizeof(TyWrittenProp) * (written_count + 1));
                    panic_if(!written, "out of memory");
                    written[written_count++] = (TyWrittenProp){ checker_atom(current_checker, cstr_sv(&i->name_string)), i->span };
                }
                if (i->computed_key) {
                    expr_typecheck(i->computed_key, errs);
                    ty_check_computed_name(current_checker, i->computed_key, i->name_span, false, errs);
                }
                if (i->shorthand_default && !(i->value.tag == EXPR_IDENTIFIER && i->value.identifier.assignment_target)) {
                    error_ctx_grammar_error_d(errs, i->shorthand_equals, 1312,
                        "Did you mean to use a ':'? An '=' can only follow a property name when the containing object literal is part of a destructuring pattern.");
                }
                if (object_item_is_private_name(i)) {
                    // tsc's checkGrammarObjectLiteralExpression; a method's is checkMethodDeclaration's (at its name, no
                    // grammar error), an accessor's TS18028 checkGrammarAccessor's
                    char const* message = "Private identifiers are not allowed outside class bodies.";
                    if (i->tag == OBJECT_ITEM_PROPERTY || i->tag == OBJECT_ITEM_SHORTHAND) {
                        error_ctx_grammar_error_d(errs, i->name_span, 18016, message);
                    } else {
                        error_ctx_error_d(errs, i->name_span, 18016, message);
                    }
                    if ((i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER) && compiler_options()->target < SCRIPT_TARGET_ES2015) {
                        error_ctx_grammar_error_d(errs, i->name_span, 18028,
                            "Private identifiers are only available when targeting ECMAScript 2015 and higher.");
                    }
                }
                if ((i->tag == OBJECT_ITEM_METHOD || i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER) &&
                    i->value.tag == EXPR_FUNCTION && !i->value.function->has_body
                ) {
                    // tsc's checkGrammarMethod/checkGrammarAccessor: at the member's last character
                    char const* end = span_end_ptr(i->span);
                    while (end > span_start_ptr(i->span) && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) end--;
                    error_ctx_grammar_error_d(errs, span_make(i->span.source, end - 1, end), 1005, "'{' expected.");
                }
                expr_typecheck(&i->value, errs);
                if (i->jsdoc_type) {
                    type_typecheck(i->jsdoc_type, errs);
                    // (At the property assignment, tsc's getErrorSpanForNode of it)
                    ty_check_assignable_expr(current_checker, expr_get_type(&i->value), ty_of_node(i->jsdoc_type),
                        i->span, &i->value, errs);
                }
                if (i->tag == OBJECT_ITEM_SPREAD) {
                    // `{ ...o }` is emitted with __assign below ES2015, `({ ...x } = o)` with __rest below ES2018
                    ScriptTarget target = compiler_options()->target;
                    if (expr_is_destructuring_target(&i->value)) {
                        if (target < SCRIPT_TARGET_ES2018) ty_check_emit_helpers(current_checker, i->span, EMIT_HELPER_REST, errs);
                    } else {
                        Ty const* type = expr_get_type(&i->value);
                        ty_check_spread_type(current_checker, type, i->span, errs);
                        ty_check_spread_overrides(current_checker, type, written, written_count, i->span, errs);
                        if (target < SCRIPT_TARGET_ES2015) ty_check_emit_helpers(current_checker, i->span, EMIT_HELPER_ASSIGN, errs);
                    }
                }
            }
            free(written);
        }   break;

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
            regex_check_literal(my inner_span, errs);
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
                // Resolving the call gives the substitutions their contextual types, so it comes before checking them
                Checker* checker = current_checker;
                LazyMemo* memo = lazy_type_state_peek((LazyType*)&my lazy_type);
                Ty const* contextual = memo && memo->done ? memo->context : nullptr;
                Expr const* tag = my template_->tag;
                if (tag->tag == EXPR_GENERIC_PARAMS) tag = &tag->generic_params->expr;
                ty_tagged_template_typecheck(checker, ty_expr_type(checker, tag), me, contextual, errs);
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
    if (my jsdoc_type) {
        type_drop(my jsdoc_type);
        free(my jsdoc_type);
    }
    expr_drop(&my value);
}

static bool expr_is_plain_identifier(Expr const* me, const char* name) {
    return my tag == EXPR_IDENTIFIER && !my identifier.keyword && !expr_is_perenthisized(me) &&
        csview_equals(my identifier.name.sv, name);
}

// `a.b` or `a["b"]`: the name b, else an invalid span
Span expr_static_property_name(Expr const* me) {
    if (my tag != EXPR_PROPERTY || expr_is_perenthisized(me) || my property->optional_chain) return span_null;
    Expr const* prop = &my property->prop;
    if (prop->tag != EXPR_LITERAL || prop->literal.tag != VALUE_STRING) return span_null;
    if (my property->form == PROPERTY_EXPR_DOT) return prop->inner_span;
    // Without the quotes
    Span ret = prop->inner_span;
    if (ret.sv.size < 2) return span_null;
    ret.sv.buf++;
    ret.sv.size -= 2;
    return ret;
}

Expr* expr_prototype_object(Expr* me) {
    Span name = expr_static_property_name(me);
    if (!span_valid(name) || !span_equals(name, "prototype")) return nullptr;
    Expr* object = &my property->expr;
    return expr_is_entity_name(object) ? object : nullptr;
}

IdentifierExpr const* expr_this_or_alias(Expr const* me) {
    if (my tag != EXPR_IDENTIFIER || expr_is_perenthisized(me)) return nullptr;
    if (my identifier.keyword == KEYWORD_IDENTIFIER_THIS) return &my identifier;
    if (my identifier.keyword || !my identifier.target) return nullptr;
    Binding const* binding = symbol_find_node(my identifier.target, SYMBOL_DECL_VARIABLE);
    if (!binding || binding->parent || binding->tag != BINDING_IDENTIFIER) return nullptr;
    Expr const* init = &((InitializedBinding const*)binding)->value;
    bool is_this = init->tag == EXPR_IDENTIFIER && init->identifier.keyword == KEYWORD_IDENTIFIER_THIS && !expr_is_perenthisized(init);
    return is_this ? &init->identifier : nullptr;
}

Symbol const* expr_entity_name_symbol(Expr const* me) {
    if (expr_is_perenthisized(me)) return nullptr;
    if (my tag == EXPR_IDENTIFIER) return my identifier.keyword ? nullptr : my identifier.target;
    if (my tag != EXPR_PROPERTY) return nullptr;
    Span name = expr_static_property_name(me);
    Symbol const* object = span_valid(name) ? expr_entity_name_symbol(&my property->expr) : nullptr;
    if (!object || object->is_module || (object->flags & SYMBOL_ALIAS) || !object->exports) return nullptr;
    return symbol_table_get(object->exports, name.sv);
}

bool expr_is_module_exports(Expr const* me) {
    Span name = expr_static_property_name(me);
    return span_valid(name) && csview_equals(name.sv, "exports") && expr_is_plain_identifier(&my property->expr, "module");
}

CommonJsTarget expr_commonjs_target(Expr const* left, Span* name_ret) {
    if (expr_is_module_exports(left)) return COMMONJS_TARGET_MODULE_EXPORTS;
    Span name = expr_static_property_name(left);
    if (!span_valid(name)) return COMMONJS_TARGET_NONE;
    Expr const* object = &left->property->expr;
    if (!expr_is_plain_identifier(object, "exports") && !expr_is_module_exports(object)) return COMMONJS_TARGET_NONE;
    if (name_ret) *name_ret = name;
    return COMMONJS_TARGET_EXPORTS_PROPERTY;
}

bool expr_symbol_is_commonjs_require(Symbol const* symbol) {
    if (!symbol || symbol == symbol->binder->require_symbol) return symbol != nullptr;
    return !(symbol->flags & SYMBOL_ALIAS) && (symbol->flags & (SYMBOL_FUNCTION | SYMBOL_VARIABLE)) && symbol->is_ambient;
}

bool expr_is_require_call(Expr const* me) {
    return my tag == EXPR_CALL && !my call->uses_new && vec_expr_size(&my call->args) == 1 &&
        expr_is_plain_identifier(&my call->callable, "require");
}

CommonJsTarget binary_commonjs_declaration(BinaryOpExpr const* assignment, Span* name_ret) {
    if (assignment->op != BINARY_OP_EQUAL) return COMMONJS_TARGET_NONE;
    SourceFile const* file = assignment->left.inner_span.source;
    if (!file || !file->is_commonjs) return COMMONJS_TARGET_NONE;
    Expr const* value = &assignment->right;
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    if (value->tag == EXPR_UNARY_OP && value->unary_op->op == UNARY_OP_VOID && value->unary_op->expr.tag == EXPR_LITERAL &&
        value->unary_op->expr.literal.tag == VALUE_NUMBER && value->unary_op->expr.literal.number == 0
    ) {
        return COMMONJS_TARGET_NONE;
    }
    return expr_commonjs_target(&assignment->left, name_ret);
}
