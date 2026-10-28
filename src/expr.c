#include "expr.h"
#include <math.h>
#include "source_file.h"
#include "stc/types.h"
#include "port_statement.h"
#include "type.h"
#include "error.h"
#include "scope.h"
#include "function_impl.h"
#include "generic.h"
#include "name_index.h"
#include "class_decl.h"
#include "interface_member.h"
#include "several_types.h"
#include "callable_type.h"
#include "flow.h"
#include "statement.h"
#include "ty/ty.h"
#include "unused.h"
#include "regex_check.h"
#include "ast_walk.h"

// Expressions are stored inline everywhere: keep them small
static_assert(sizeof(Expr) == 72, "Expr grew");

const Expr expr_implicit_any_no_span = { .tag = EXPR_IMPLICIT_ANY };
const int as_type_precedence = 90;
const int ternary_precedence = 20;
const int new_keyword_precedence = 170;

const CallRare call_no_rare = {0};
const PropertyRare property_no_rare = {0};

PropertyRare* property_rare_mut(PropertyExpr* me) {
    if (!my rare) my rare = ast_new(PropertyRare, {0});
    return my rare;
}

CallRare* call_rare_mut(CallExpr* me) {
    if (!my rare) my rare = ast_new(CallRare, {0});
    return my rare;
}

// A dot access is allocated only up to its name
static const isize property_dot_size = offsetof(PropertyExpr, name_) + sizeof(Span);

PropertyExpr* property_dot_new(PropertyExpr const* fields) {
    check(fields->form == PROPERTY_EXPR_DOT);
    void* raw = ast_malloc(property_dot_size);
    memcpy(raw, fields, property_dot_size);
    return raw;
}

void identifier_expr_drop(IdentifierExpr* me) {
    ignore me;
}

// tsc's checkThisBeforeSuper: directly in the constructor of a derived class (not `extends null`), `this` and
// `super.x` need a `super()` call on every way there. The flow graph behind the current node is complete already
static bool is_valid_const_assertion_argument(Expr const* me);
static bool check_before_super(IdentifierExpr const* me, Scope const* names, unsigned code, char const* what, ErrorCtx* errs) {
    FlowBuilder* flow = scope_flow(names);
    if (!flow || flow->dead || !flow->constructor_of || !names->function || !names->function->is_constructor) return true;
    ClassDecl const* class_decl = symbol_find_node(flow->constructor_of, SYMBOL_DECL_CLASS);
    if (!class_decl || class_decl->extends.tag == EXPR_IMPLICIT_ANY) return true;
    if (class_decl->extends.tag == EXPR_LITERAL && class_decl->extends.literal.tag == VALUE_NULL) return true;
    if (flow_is_post_super(flow->current)) return true;
    error_ctx_error_d(errs, identifier_expr_name(me), code, "'super' must be called before accessing ", what,
        " in the constructor of a derived class.");
    return false;
}

// Whether a declaration of a `Reflect` symbol emits a runtime binding (tsc's needCollisionCheckForIdentifier)
static bool reflect_decl_is_runtime(Scope const* scope, SymbolDecl const* decl) {
    if (scope->ambient) return false;
    switch (decl->kind) {
        case SYMBOL_DECL_VARIABLE: return !((Binding const*)decl->node)->ambient;
        case SYMBOL_DECL_FUNCTION: return !((FunctionImpl const*)decl->node)->ambient;
        case SYMBOL_DECL_CLASS: return !((ClassDecl const*)decl->node)->is_ambient;
        case SYMBOL_DECL_NAMESPACE: { // (Even an uninstantiated one)
            Scope const* inner = ((NamespaceDecl const*)decl->node)->inner_scope;
            return !inner || !inner->ambient;
        }
        case SYMBOL_DECL_ENUM:
            return true;
        case SYMBOL_DECL_ALIAS: {
            PortItem const* item = decl->node;
            return !item->type_only && !port_item_is_export(item);
        }
        default:
            return false;
    }
}

// tsc's checkReflectCollision: `super.x` in a static initializer is emitted through `Reflect` for targets ES2015 to
// ES2021, so a runtime declaration named `Reflect` in any block scope around it collides (tsc marks every enclosing
// block scope container with ContainsSuperPropertyInStaticInitializer). A script's top level is exempt. Each `super`
// reports again, the diagnostics dedupe
static void check_reflect_collisions(Scope const* names) {
    CompilerOptions const* options = compiler_options();
    if (options->target > SCRIPT_TARGET_ES2021 || options->no_emit) return;
    report_emit_name_collisions(names, "Reflect", false);
}

// The runtime declarations named `name` in the block scopes around `names` (tsc's needCollisionCheckForIdentifier
// against a NodeCheckFlags set on every enclosing block scope container), in the scopes' own files: Reflect (TS2818,
// not at a script's top level) or WeakMap/WeakSet (TS18027)
void report_emit_name_collisions(Scope const* names, char const* name, bool private_names) {
    for (Scope const* scope = names; scope; scope = scope->parent) {
        if (scope->kind == SCOPE_GLOBAL || (!private_names && scope->kind == SCOPE_SCRIPT)) break;
        Symbol const* found = symbol_table_get(scope_locals(scope), csview_from(name));
        if (!found && (scope->kind == SCOPE_MODULE || scope->kind == SCOPE_NAMESPACE) && scope->container) {
            found = symbol_table_get(scope->container->exports, csview_from(name));
        }
        if (!found) continue;
        for (u32 i = 0; i < found->decl_count; i++) {
            SymbolDecl const* decl = &found->decls[i];
            if (!reflect_decl_is_runtime(scope, decl)) continue;
            // A `var` in a nested block is in that block's block scope container, which isn't around the `super`
            if (decl->block && decl->block != scope) {
                bool around = false;
                for (Scope const* s = names; s && !around; s = s->parent) around = s == decl->block;
                if (!around) continue;
            }
            // (tsc reports an import specifier, `{ A as Reflect }`, at the whole specifier)
            Span at = decl->name;
            if (decl->kind == SYMBOL_DECL_ALIAS) {
                PortItem const* item = decl->node;
                if (item->tag == PORT_IMPORT_SPECIFIC && span_valid(item->source_name)) {
                    at = span_between(item->source_name, item->decl_name);
                }
            }
            if (private_names) {
                error_ctx_error_d(binder_errs(scope->binder), at, 18027, "Compiler reserves name '", name,
                    "' when emitting private identifier downlevel.");
            } else {
                error_ctx_error_d(binder_errs(scope->binder), at, 2818, "Duplicate identifier 'Reflect'. Compiler reserves name ",
                    "'Reflect' when emitting 'super' references in static initializers.");
            }
        }
    }
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
    bool through_arrow = false; // (tsc's isInConstructorArgumentInitializer stops at a function)
    for (; scope; scope = scope->parent) {
        if (scope->no_super == NO_SUPER_LITERAL_COMPUTED_NAME || scope->no_super == NO_SUPER_LITERAL_METHOD_COMPUTED_NAME) {
            in_computed_name = true;
            continue;
        }
        if (scope->no_super == NO_SUPER_COMPUTED_NAME || scope->no_super == NO_SUPER_DECORATOR) {
            in_computed_name |= scope->no_super == NO_SUPER_COMPUTED_NAME;
            skip_class = true;
            continue;
        }
        if (skip_class) {
            skip_class = scope_rare(scope)->this_ref.kind != THIS_CLASS_INSTANCE && scope_rare(scope)->this_ref.kind != THIS_CLASS_STATIC;
            continue;
        }
        if (scope->kind == SCOPE_FUNCTION && scope->function) {
            FunctionImpl const* function = scope->function;
            if (function_impl_is_arrow(function) && !is_call) {
                through_arrow = true;
                continue;
            }
            bool member = span_valid(function_impl_member_name(function)) || function->accessor;
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
        if (scope->kind == SCOPE_STATIC_BLOCK || scope_rare(scope)->this_ref.kind == THIS_CLASS_INSTANCE ||
            scope_rare(scope)->this_ref.kind == THIS_CLASS_STATIC
        ) {
            legal = !is_call;
            if (legal && (scope->kind == SCOPE_STATIC_BLOCK || scope_rare(scope)->this_ref.kind == THIS_CLASS_STATIC)) {
                check_reflect_collisions(names);
            }
            break;
        }
    }
    if (!legal) {
        my super_invalid = true;
        if (in_computed_name) {
            error_ctx_error_d(errs, identifier_expr_name(me), 2466, "'super' cannot be referenced in a computed property name.");
        } else if (is_call) {
            error_ctx_error_d(errs, identifier_expr_name(me), 2337,
                "Super calls are not permitted outside constructors or in nested functions inside constructors.");
        } else {
            error_ctx_error_d(errs, identifier_expr_name(me), 2660,
                "'super' can only be referenced in members of derived classes or object literal expressions.");
        }
        return;
    }
    bool in_constructor_args = !through_arrow && scope->no_super == NO_SUPER_CONSTRUCTOR_ARGS;
    while (scope && scope_rare(scope)->this_ref.kind != THIS_CLASS_INSTANCE && scope_rare(scope)->this_ref.kind != THIS_CLASS_STATIC) {
        scope = scope->parent;
    }
    ClassDecl const* class_decl = scope ? scope_rare(scope)->this_ref.class_decl : nullptr;
    if (!class_decl) return;
    if (class_decl->extends.tag == EXPR_IMPLICIT_ANY) {
        my super_invalid = true;
        error_ctx_error_d(errs, identifier_expr_name(me), 2335, "'super' can only be referenced in a derived class.");
        return;
    }
    if (!is_call) check_before_super(me, names, 17011, "a property of 'super'", errs);
    if (in_constructor_args) {
        my super_invalid = true;
        error_ctx_error_d(errs, identifier_expr_name(me), 2336, "'super' cannot be referenced in constructor arguments.");
        return;
    }
    my this_kind = scope_rare(scope)->this_ref.kind;
    my target = scope_rare(scope)->this_ref.symbol;
}

// Whether a use of a class is in an ES decorator of the class or of one of its members, not deferred by a function
// (an IIFE runs right away): the class isn't initialized yet (tsc's isBlockScopedNameDeclaredBeforeUse)
static bool class_decorator_runs_first(Scope const* names, void const* class_decl) {
    if (compiler_options()->experimental_decorators) return false;
    for (Scope const* i = names; i; i = i->parent) {
        if (i->kind == SCOPE_FUNCTION && !(i->function && i->function->is_iife)) return false;
        if (scope_rare(i)->decorator_of) return scope_rare(i)->decorator_of == class_decl;
        if (i->no_super == NO_SUPER_DECORATOR) {
            Scope const* class_scope = i;
            while (class_scope && !scope_rare(class_scope)->this_ref.class_decl) class_scope = class_scope->parent;
            return class_scope && scope_rare(class_scope)->this_ref.class_decl == class_decl;
        }
    }
    return false;
}

// tsc's checkResolvedBlockScopedVariable / isBlockScopedNameDeclaredBeforeUse: a `let`/`const`, class or enum used
// (as a value) before its declaration, unless the use is deferred: in a function, in an instance member
__attribute__((always_inline)) static inline void check_declared_before_use(IdentifierExpr const* me, Scope const* names, ErrorCtx* errs) {
    Symbol const* symbol = my symbol;
    // (An import merged with an exported value: the value, see scope_get_value())
    if ((symbol->flags & SYMBOL_ALIAS) && my target && symbol->alias && symbol->alias->export_value == my target) symbol = my target;
    u32 flags = symbol->flags;
    if (!(flags & (SYMBOL_BLOCK_SCOPED_VARIABLE | SYMBOL_CLASS | SYMBOL_ENUM)) || symbol->is_ambient) return;
    if (flags & SYMBOL_CLASS && flags & (SYMBOL_FUNCTION | SYMBOL_FUNCTION_SCOPED_VARIABLE)) return;
    if ((flags & SYMBOL_ENUM) == SYMBOL_CONST_ENUM && !compiler_options()->isolated_modules) return;
    if ((scope_flow(names) && scope_flow(names)->in_type_query) || names->ambient) return;
    SymbolDecl const* decl = nullptr;
    for (u32 i = 0; i < symbol->decl_count && !decl; i++) {
        SymbolDeclKind kind = symbol->decls[i].kind;
        if ((kind == SYMBOL_DECL_VARIABLE && symbol->decls[i].flags & SYMBOL_BLOCK_SCOPED_VARIABLE) ||
            kind == SYMBOL_DECL_CLASS || kind == SYMBOL_DECL_ENUM
        ) {
            decl = &symbol->decls[i];
        }
    }
    if (!decl || !span_source(decl->full)) return;
    // (Files aren't ordered without an outFile, which TypeScript 7 removed)
    if (span_source(decl->full) != span_source(identifier_expr_name(me))) return;
    if (span_start_ptr(decl->full) <= span_start_ptr(identifier_expr_name(me))) {
        if (decl->kind == SYMBOL_DECL_CLASS) {
            // Declared before, but used in a computed name of its own members (`class A { [A.p]() {} }`)
            bool in_computed_name = false;
            Scope const* i = names;
            for (; i && i->kind != SCOPE_FUNCTION && !scope_rare(i)->this_ref.class_decl; i = i->parent) {
                in_computed_name |= i->no_super == NO_SUPER_COMPUTED_NAME;
            }
            if (in_computed_name && i && scope_rare(i)->this_ref.class_decl == decl->node) {
                // (reported below)
            } else if (!class_decorator_runs_first(names, decl->node)) {
                return;
            }
        } else {
            // Declared before, but used in its own initializer (`let x = x`, `for (const x of x)`)
            if (decl->kind != SYMBOL_DECL_VARIABLE || !scope_flow(names) || !scope_flow(names)->initializing) return;
            Binding const* binding = decl->node;
            Binding const* root = binding;
            while (root->parent) root = root->parent;
            if (root != &scope_flow(names)->initializing->binding) return;
            // In the default of another element of the pattern it's initialized already (`{ a, b = a }`)
            char const* at = span_start_ptr(identifier_expr_name(me));
            bool in_pattern = at >= span_start_ptr(root->content_span) && at < span_end_ptr(root->content_span);
            Span element = binding->has_element_span ? binding_element_span(binding) : binding->content_span;
            if (binding->default_value) element = span_between(element, expr_outer_span(binding->default_value));
            // (the element starts at its property name: `{ [a]: a }`)
            if (span_start_ptr(decl->full) < span_start_ptr(element)) element = span_between(decl->full, element);
            if (in_pattern && !(at >= span_start_ptr(element) && at < span_end_ptr(element))) return;
        }
    } else {
        // Used first: fine if the use is deferred until after the declaration ran
        bool through_computed_name = false;
        for (Scope const* i = names; i && !scope_declares_symbol(i, symbol); i = i->parent) {
            // (Not an immediately invoked one, async or not, which runs right there: tsc 6.0)
            if (i->kind == SCOPE_FUNCTION && !(i->function && i->function->is_iife)) return;
            // A computed member name runs with the class, the members don't. Neither do their decorators (and their
            // parameters'): tsc 6.0's isUsedInFunctionOrInstancePropertyWorker asks about the class instead
            if (i->no_super == NO_SUPER_COMPUTED_NAME || i->member_decorator) through_computed_name = true;
            if (scope_rare(i)->this_ref.kind == THIS_CLASS_INSTANCE && !through_computed_name) return;
            if (scope_rare(i)->this_ref.class_decl) through_computed_name = false;
        }
    }
    Error* error;
    if (flags & SYMBOL_BLOCK_SCOPED_VARIABLE) {
        error = error_ctx_error_d(errs, identifier_expr_name(me), 2448, "Block-scoped variable '", decl->name, "' used before its declaration.");
    } else if (flags & SYMBOL_CLASS) {
        error = error_ctx_error_d(errs, identifier_expr_name(me), 2449, "Class '", decl->name, "' used before its declaration.");
    } else {
        error = error_ctx_error_d(errs, identifier_expr_name(me), 2450, "Enum '", decl->name, "' used before its declaration.");
    }
    error_add_related_d(error, decl->name, 2728, "'", decl->name, "' is declared here.");
}

// tsc's onSuccessfullyResolvedSymbol for parameter initializers: a parameter's initializer (or its pattern's defaults
// and computed names) runs before the parameters after it are, and before it is itself
static void check_parameter_reference(IdentifierExpr const* me, Scope const* names, ErrorCtx* errs) {
    FlowBuilder const* flow = scope_flow(names);
    Binding const* param = flow ? flow->parameter_initializing : nullptr;
    if (!param || flow->in_type_query) return;
    Symbol const* symbol = my symbol;
    Span param_name = param->content_span;
    if (param->tag == BINDING_IDENTIFIER && symbol == param->symbol) {
        error_ctx_error_d(errs, identifier_expr_name(me), 2372, "Parameter '", param_name, "' cannot reference itself.");
    } else if (span_start_ptr(symbol->name_span) > span_start_ptr(param_name) && span_source(symbol->name_span) == span_source(param_name) &&
        flow->parameter_function_scope && scope_declares_symbol(flow->parameter_function_scope, symbol)
    ) {
        error_ctx_error_d(errs, identifier_expr_name(me), 2373,
            "Parameter '", param_name, "' cannot reference identifier '", identifier_expr_name(me), "' declared after it.");
    }
}

// Whether a function is around that has `arguments` (an arrow function sees the arguments of the function it is in)
static bool scope_has_arguments(Scope const* scope) {
    for (; scope; scope = scope->parent) {
        // (Not a signature: tsc's resolveName gives `arguments` to function declarations and expressions, methods,
        // constructors and accessors only)
        if (scope->kind == SCOPE_FUNCTION && scope->function && !scope->function->is_type &&
            !function_impl_is_arrow(scope->function)
        ) {
            return true;
        }
    }
    return false;
}

// The flow builder of the generator whose body a `yield` is in, as tsc's forEachYieldExpression finds them: not in its
// parameters' initializers, nor in the decorators of a method or accessor of a class in it (a function-like node it
// doesn't enter), but in a field's initializer of such a class
static FlowBuilder* yield_generator_body_flow(Scope const* names) {
    for (Scope const* scope = names; scope; scope = scope->parent) {
        if (scope->kind == SCOPE_FUNCTION) return scope->function && scope->function->is_generator ? scope_flow(scope) : nullptr;
        if (scope->parameters) return nullptr;
        if (scope->member_decorator && !(scope->parent && scope->parent->in_field)) return nullptr;
    }
    return nullptr;
}

// In a class field (its initializer, computed name or decorators) or static block, not in a function or arrow function
// in it (tsc's isInPropertyInitializerOrClassStaticBlock)
static bool in_initializer_or_static_block(Scope const* scope) {
    for (; scope; scope = scope->parent) {
        // (Through arrow functions: tsc 6.0's isInPropertyInitializerOrClassStaticBlock with ignoreArrowFunctions)
        if (scope->kind == SCOPE_FUNCTION && !(scope->function && function_impl_is_arrow(scope->function))) return false;
        if (scope->in_field || scope->kind == SCOPE_STATIC_BLOCK) return true;
    }
    return false;
}

// A reference to `arguments` directly in the body of a JavaScript function (not its parameters, nor an arrow function in
// it) gives its signature a rest parameter (tsc's containsArgumentsReference, maybeAddJsSyntheticRestParameter)
static void note_arguments_reference(Scope const* names) {
    if (!names->file || !names->file->is_js) return;
    for (Scope const* scope = names; scope; scope = scope->parent) {
        if (scope->parameters) return;
        if (scope->kind == SCOPE_FUNCTION) {
            if (scope->function && !function_impl_is_arrow(scope->function)) scope->function->uses_arguments = true;
            return;
        }
    }
}

// A span extended back over the `type` keyword in front of it, if there is one (`type a` of an import specifier)
static Span span_from_type_keyword(Span name) {
    char const* start = span_start_ptr(name);
    char const* file_start = span_source(name) ? cstr_str(&span_source(name)->contents) : start;
    while (start > file_start && (start[-1] == ' ' || start[-1] == '\t' || start[-1] == '\n' || start[-1] == '\r')) start--;
    if (start - file_start < 4 || memcmp(start - 4, "type", 4)) return name;
    char const* before = start - 4;
    if (before > file_start && (isalnum((unsigned char)before[-1]) || before[-1] == '_' || before[-1] == '$')) return name;
    return span_make_in(name, before, span_end_ptr(name));
}

// tsc's addTypeOnlyDeclarationRelatedInfo: TS1376/TS1377 on the type-only import or export `name` comes through
void port_item_add_type_only_related(Error* error, PortItem const* item, csview name) {
    if (!error || !item) return;
    bool exported = port_item_is_export(item);
    // (tsc's node: the whole `export type *` declaration, the `* as ns` of a namespace export, the whole
    // `import A = require()`, the import clause of a default import, the whole specifier)
    Span at = item->decl_name;
    if (item->tag == PORT_REEXPORT_SPLAT || item->tag == PORT_REEXPORT_GATHER || item->tag == PORT_IMPORT_ALIAS ||
        item->is_require
    ) {
        at = item->full_span;
    } else if (item->tag == PORT_IMPORT_DEFAULT && item->type_only) {
        at = span_from_type_keyword(item->decl_name); // (from its `type`)
    } else if (item->tag == PORT_IMPORT_SPECIFIC || item->tag == PORT_EXPORT_SPECIFIC || item->tag == PORT_REEXPORT_SPECIFIC) {
        if (span_valid(item->source_name) && span_start_ptr(item->source_name) < span_start_ptr(item->decl_name)) {
            at = item->source_name;
        }
        // (A specifier's own `type` is part of it: `type a`)
        if (item->type_modifier) at = span_from_type_keyword(at);
    }
    error_add_related_d(error, at, exported ? 1377 : 1376, "'", name, "' was ", exported ? "exported" : "imported", " here.");
}

// TS1361/TS1362: a value that was imported or exported as a type only (tsc's
// checkAndReportErrorForResolvingImportAliasToTypeOnlySymbol). Fine in `typeof x` types and ambient contexts
// A value use of `x` after an `import x = A.B` that resolved to nothing: tsc checks `A.B` as an expression then
// (markAliasSymbolAsReferenced), which says `A` isn't found as a value either. Once per import, in its own file. Of an
// import in a namespace (which resolves nothing itself): its module's TS2307
static void check_unresolved_import_alias_value(IdentifierExpr const* me, Scope const* names) {
    Symbol const* symbol = my symbol;
    if (!symbol || !(symbol->flags & SYMBOL_ALIAS) || !symbol->alias || !symbol->alias->item || !my target->is_placeholder) return;
    PortItem* item = symbol->alias->item;
    Scope const* scope = symbol->alias->scope;
    if (item->value_use_checked || !scope || scope->file != names->file) return;
    if (port_item_check_namespace_import_use(symbol, names)) return;
    if (item->tag != PORT_IMPORT_ALIAS || !vec_span_size(&port_item_alias(item)->path)) return;
    item->value_use_checked = true;
    port_item_report_alias_root_value(scope, *vec_span_front(&port_item_alias(item)->path), binder_errs(scope->binder));
}

// (`at` is the name the value use spells: an identifier, or a JSX tag whose factory `found` is)
static void check_type_only_value_at(Symbol const* found, Span at, csview name, Scope const* names, ErrorCtx* errs) {
    if (!found || !(found->flags & SYMBOL_ALIAS) || found->flags & SYMBOL_VALUE) return;
    if (scope_flow(names) && (scope_flow(names)->in_type_query || scope_flow(names)->type_only_alias_ok)) return;
    for (Scope const* scope = names; scope; scope = scope->parent) {
        if (scope->ambient) return;
    }
    Symbol const* named = nullptr;
    PortItem const* item = symbol_type_only_declaration_naming(found, &named);
    // (what the type-only declaration names has to be a value: a merge further down the chain may be what is)
    if (!named) named = found;
    if (!item || !(symbol_resolved_flags(named) & SYMBOL_VALUE)) return;
    // (A type-only default import whose own module isn't found (TS2307): tsc's getTargetOfImportClause, or
    // getTargetOfImportSpecifier for `{ default as x }`, returns before markSymbolOfAliasDeclarationIfTypeOnly, so
    // nothing is type-only. Named and namespace imports are marked whatever their module, as is a chain that ends in
    // a missing member further down)
    bool default_import = item->tag == PORT_IMPORT_DEFAULT ||
        (item->tag == PORT_IMPORT_SPECIFIC && csview_equals(port_item_name(item->source_name), "default"));
    if (default_import && item->module_ref && !item->module_ref->file && item->symbol &&
        !binder_module_of_ref(item->symbol->binder, item->module_ref)
    ) {
        return;
    }
    bool exported = port_item_is_export(item);
    Error* error = error_ctx_error_d(errs, at, exported ? 1362 : 1361, "'", name, "' cannot be used as a value "
        "because it was ", exported ? "exported using 'export type'." : "imported using 'import type'.");
    port_item_add_type_only_related(error, item, name);
}

static void check_type_only_value(IdentifierExpr const* me, Scope const* names, ErrorCtx* errs) {
    check_type_only_value_at(my symbol, identifier_expr_name(me), span_sv(identifier_expr_name(me)), names, errs);
}

// What there is to say about a module name that an import-like construct uses
static void module_ref_report_in(ModuleRef* ref, Scope* names, ErrorCtx* errs, bool emittable) {
    if (!ref) return;
    if (ref->file) {
        module_ref_report_resolved(ref, false, !emittable, errs);
    } else if (!binder_module_of_ref(names->binder, ref)) {
        module_ref_report_unresolved(ref, errs);
    }
}

void module_ref_report(ModuleRef* ref, Scope* names, ErrorCtx* errs) { module_ref_report_in(ref, names, errs, true); }
void module_ref_report_require(ModuleRef* ref, Scope* names, ErrorCtx* errs) { module_ref_report_in(ref, names, errs, false); }

// A JavaScript `require("m")` call that isn't an alias (tsc's checkRequireCall -> resolveExternalModuleName): a file
// that isn't a module is TS2306 too
static void require_call_report(ModuleRef* ref, Scope* names, ErrorCtx* errs) {
    module_ref_report_require(ref, names, errs);
    if (ref && ref->file && !binder_module_of_ref(names->binder, ref)) {
        cstr shown = module_ref_file_message_path(ref);
        error_ctx_error_d(errs, ref->span, 2306, "File '", shown, "' is not a module.");
        cstr_drop(&shown);
    }
}

// What an entity name expression (`a`, `a.b.c`) names with `meaning`, quietly: null if nothing
static Symbol const* entity_name_symbol(Scope* names, Expr const* expr, u32 meaning) {
    if (expr->tag == EXPR_IDENTIFIER && !expr->identifier.keyword) {
        return symbol_for_meaning(scope_resolve(names, span_sv(expr->inner_span), meaning), meaning);
    }
    if (expr->tag != EXPR_PROPERTY || expr->property->form != PROPERTY_EXPR_DOT) return nullptr;
    Symbol const* container = entity_name_symbol(names, &expr->property->expr, SYMBOL_NAMESPACE);
    if (!container || container->is_placeholder) return nullptr;
    return symbol_for_meaning(symbol_get_export(container, name_cooked(span_sv(property_name(expr->property)))), meaning);
}

// A class's `extends` names an interface (tsc's checkAndReportErrorForExtendingInterface)
static bool entity_names_interface(Scope* names, Expr const* entity) {
    return entity_name_symbol(names, entity, SYMBOL_INTERFACE) != nullptr;
}

// In a constructor and not in a function, arrow function or class field in it (tsc's isThisContainerOrFunctionBlock:
// a nested class's property declaration, its decorators and computed name included; a method's decorators count)
static bool directly_in_constructor(Scope const* names) {
    FunctionImpl const* constructor = names->function;
    if (!constructor || !constructor->is_constructor) return false;
    for (Scope const* scope = names; scope; scope = scope->parent) {
        if (scope->kind == SCOPE_FUNCTION && scope->function == constructor) return true;
        if (scope->in_field) return false;
    }
    return false;
}

// tsc's resolveNameHelper: a module can't use the value of a UMD global (`export as namespace Foo` of a declaration
// file) without an import, unless allowUmdGlobalAccess
static void check_umd_global_symbol_use(Symbol const* symbol, Span at, Scope const* names, ErrorCtx* errs);
static void check_umd_global_use(IdentifierExpr const* me, Scope const* names, ErrorCtx* errs) {
    check_umd_global_symbol_use(my symbol, identifier_expr_name(me), names, errs);
}

static void check_umd_global_symbol_use(Symbol const* symbol, Span at, Scope const* names, ErrorCtx* errs) {
    if (!symbol || !symbol->decl_count || !names->file || !names->file->is_module) return;
    if (compiler_options()->allow_umd_global_access) return;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->kind != SYMBOL_DECL_ALIAS || !decl->node || ((PortItem const*)decl->node)->tag != PORT_EXPORT_AS_NAMESPACE) return;
    }
    error_ctx_error_d(errs, at, 2686, "'", symbol->name, "' refers to a UMD global, but the current file is a module. ",
        "Consider adding an import instead.");
}

// tsc's resolveNameHelper under isolatedModules (the option itself, not verbatimModuleSyntax's, which reports the
// import already): a global value found past an import of the same name in a module, which only brings a type or
// namespace, would be shadowed once the import is gone (TS2866 at the import)
static void check_isolated_global_shadow(IdentifierExpr const* me, Scope const* names, ErrorCtx* errs) {
    Symbol const* symbol = my symbol;
    // (Cheap tests first: most names are local, and most files don't set the option)
    if (!symbol || symbol->is_local || symbol->parent) return;
    CompilerOptions const* options = compiler_options();
    if (!options->is_set[COMPILER_OPTION_isolated_modules] || !options->isolated_modules) return;
    if (!names->file || !names->file->is_module || !(scope_flow(names) && !scope_flow(names)->in_type_query)) return;
    csview name = span_sv(identifier_expr_name(me));
    Scope const* module = names;
    while (module && module->kind != SCOPE_MODULE) module = module->parent;
    Symbol const* local = module ? symbol_table_get(scope_locals(module), name) : nullptr;
    if (!local || !(local->flags & SYMBOL_ALIAS) || symbol_table_get(&names->binder->globals, name) != symbol) return;
    if (!(symbol_resolved_flags(local) & ~SYMBOL_VALUE)) return;
    for (u32 i = 0; i < local->decl_count; i++) {
        SymbolDecl const* decl = &local->decls[i];
        PortItem const* item = decl->kind == SYMBOL_DECL_ALIAS ? decl->node : nullptr;
        if (!item || item->jsdoc) continue;
        if (item->tag != PORT_IMPORT_DEFAULT && item->tag != PORT_IMPORT_GATHER && item->tag != PORT_IMPORT_SPECIFIC &&
            item->tag != PORT_IMPORT_ALIAS
        ) {
            continue;
        }
        if (item->type_only || item->type_modifier) return;
        Span at = item->tag == PORT_IMPORT_SPECIFIC && span_valid(item->source_name) ? span_between(item->source_name, item->decl_name) :
            item->decl_name;
        error_ctx_error_d(errs, at, 2866, "Import '", identifier_expr_name(me), "' conflicts with global value used in this file, "
            "so must be declared with a type-only import when 'isolatedModules' is enabled.");
        return;
    }
}

// tsc's resolveNameHelper at an enum: a member of the enum declared in another file, unqualified, under isolatedModules
// (Called only for an enum member under isolatedModules: the common path pays one flag test)
__attribute__((noinline, cold)) static void check_isolated_enum_member(IdentifierExpr const* me, Scope const* names, ErrorCtx* errs) {
    Symbol const* symbol = my symbol;
    CompilerOptions const* options = compiler_options();
    Scope const* enum_scope = names;
    while (enum_scope && enum_scope->kind != SCOPE_ENUM) enum_scope = enum_scope->parent;
    if (!enum_scope || enum_scope->ambient || !enum_scope->container) return;
    SymbolDecl const* decl = symbol_find_decl(symbol, SYMBOL_DECL_ENUM_MEMBER);
    if (!decl || span_source(decl->full) == span_source(identifier_expr_name(me))) return;
    error_ctx_error_d(errs, identifier_expr_name(me), 1281, "Cannot access '", identifier_expr_name(me), "' from another file "
        "without qualification when '", options->verbatim_module_syntax ? "verbatimModuleSyntax" : "isolatedModules",
        "' is enabled. Use '", enum_scope->container->name, ".", identifier_expr_name(me), "' instead.");
}

// tsgo's isPrimitiveTypeName (checkAndReportErrorForUsingTypeAsValue)
static bool is_value_primitive_type_name(csview name) {
    return csview_equals(name, "any") || csview_equals(name, "string") || csview_equals(name, "number") ||
        csview_equals(name, "boolean") || csview_equals(name, "never") || csview_equals(name, "unknown");
}

void identifier_expr_lookup_names(IdentifierExpr* me, Scope* names, ErrorCtx* errs) {
    if (my keyword) {
        if ((my keyword == KEYWORD_IDENTIFIER_THIS || (my keyword == KEYWORD_IDENTIFIER_SUPER && !my super_call)) &&
            directly_in_constructor(names) && !span_valid(function_impl_rare(names->function)->first_this_or_super_)
        ) {
            function_impl_rare_mut(names->function)->first_this_or_super_ = identifier_expr_name(me);
        }
        if (my keyword == KEYWORD_IDENTIFIER_THIS) {
            scope_note_this_reference(names);
            ThisRef this_ref = scope_get_this(names, false);
            my this_kind = this_ref.kind;
            my target = this_ref.symbol;
            my this_function_ = this_ref.function;
            if (this_ref.kind == THIS_ANY) {
                Scope const* container = names;
                while (container && !scope_rare(container)->this_ref.kind) container = container->parent;
                ThisRef outer_ref = container && container->parent ? scope_get_this(container->parent, false) : (ThisRef){0};
                ThisKind outer = outer_ref.kind;
                my this_shadows_outer = outer == THIS_CLASS_INSTANCE || outer == THIS_CLASS_STATIC || outer == THIS_PARAMETER;
                if (!outer && container && container->parent) {
                    // At the top level: a module's `this` (undefined, or what a CommonJS module exports) isn't globalThis
                    // (tsc's tryGetThisTypeAt of the container)
                    Scope const* file = container->parent;
                    while (file->parent && file->kind != SCOPE_MODULE && file->kind != SCOPE_SCRIPT && file->kind != SCOPE_NAMESPACE &&
                        file->kind != SCOPE_ENUM
                    ) {
                        file = file->parent;
                    }
                    my this_shadows_outer = file->kind == SCOPE_MODULE && !(file->file && file->file->is_commonjs);
                }
                // (A function's `this` shadows another's when that one has a type, which the checker knows)
                if (outer == THIS_ANY && outer_ref.function && this_ref.function && this_ref.function != outer_ref.function) {
                    function_impl_rare_mut((FunctionImpl*)this_ref.function)->outer_this_function = outer_ref.function;
                }
            }
            check_before_super(me, names, 17009, "'this'", errs);
            // tsc's checkThisInStaticClassFieldInitializerInDecoratedClass: up to the this container (through arrows)
            for (Scope const* scope = names; scope && !scope_rare(scope)->this_ref.kind; scope = scope->parent) {
                if (scope->kind == SCOPE_FUNCTION && !(scope->function && function_impl_is_arrow(scope->function))) break;
                if (!scope->decorated_static_field) continue;
                error_ctx_error_d(errs, identifier_expr_name(me), 2816, "Cannot use 'this' in a static property initializer of a decorated class.");
                break;
            }
            // tsc's checkThisExpression: the this container (arrow functions count) is a class member's computed name
            for (Scope const* scope = names; scope && scope->kind != SCOPE_FUNCTION && !scope_rare(scope)->this_ref.kind; scope = scope->parent) {
                if (scope->no_super != NO_SUPER_COMPUTED_NAME) continue;
                error_ctx_error_d(errs, identifier_expr_name(me), 2465, "'this' cannot be referenced in a computed property name.");
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
                // (A CommonJS file isn't a module there: tsc's tryGetThisTypeAt asks for an externalModuleIndicator)
                if (file->kind == SCOPE_SCRIPT || (file->kind == SCOPE_MODULE && file->file && file->file->is_commonjs)) {
                    my this_kind = THIS_GLOBAL;
                    for (Scope const* scope = names; scope && scope != file; scope = scope->parent) {
                        if (scope->kind == SCOPE_FUNCTION && scope->function && function_impl_is_arrow(scope->function)) {
                            my this_through_arrow = true;
                        }
                    }
                }
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
                !(scope->kind == SCOPE_FUNCTION && scope->function && !function_impl_is_arrow(scope->function))
            ) {
                scope = scope->parent;
            }
            FunctionImpl const* function = scope && scope->kind == SCOPE_FUNCTION ? scope->function : nullptr;
            if (function && function->is_constructor) {
                my target = function_impl_rare(function)->constructed_class;
            } else if (function && !function->is_method && !function->accessor && !span_valid(function_impl_member_name(function))) {
                my target = function->symbol;
                my this_function_ = function;
            } else {
                error_ctx_error_d(errs, identifier_expr_name(me), 17013, "Meta-property 'new.target' is only allowed in the body of a "
                    "function declaration, function expression, or constructor.");
            }
        } else if (my keyword == KEYWORD_IDENTIFIER_IMPORT && identifier_module_ref(me)) {
            // `import("./a")`
            if (identifier_module_ref(me)->file) {
                module_ref_report_resolved(identifier_module_ref(me), false, false, errs);
            } else if (!binder_module_of_ref(names->binder, identifier_module_ref(me))) {
                module_ref_report_unresolved(identifier_module_ref(me), errs);
            }
        }
        return;
    }
    // (`default` is a reserved word, which only `typeof default` looks up: never the module's default export, which
    // tsc keeps in the module's exports only)
    my target = csview_equals(span_sv(identifier_expr_name(me)), "default") ? nullptr : scope_get_value(names, span_sv(identifier_expr_name(me)), &my symbol);
    if (my require_call && my target && names->file && names->file->is_js && expr_symbol_is_commonjs_require(my target)) {
        // An ambient `require` (@types/node's) is tsc's requireSymbol too (isCommonJsRequire)
        if (my require_call != 2) require_call_report(identifier_module_ref(me), names, errs);
        return;
    }
    if (!my target && names->file && names->file->is_js) {
        if (my require_call) {
            // tsc's requireSymbol, and the module like an import's (a declaration's aliases report it themselves)
            my target = my symbol = binder_require_symbol(names->binder);
            if (my require_call != 2) require_call_report(identifier_module_ref(me), names, errs);
            return;
        }
    }
    if (my target) {
        ClassItem const* field = scope_invalid_initializer_field(names, span_sv(identifier_expr_name(me)), my symbol);
        if (field) {
            // Unresolved (any) like in tsc's resolveNameHelper, but the symbols baseline still shows what the name
            // finds (getSymbolAtLocation doesn't report)
            scope_report_invalid_initializer_reference(field, identifier_expr_name(me), errs);
            my target = nullptr;
            my invalid_initializer_reference = true;
            return;
        }
        check_type_only_value(me, names, errs);
        check_unresolved_import_alias_value(me, names);
        check_declared_before_use(me, names, errs);
        check_parameter_reference(me, names, errs);
        check_umd_global_use(me, names, errs);
        if (__builtin_expect(my symbol->flags & SYMBOL_ENUM_MEMBER, 0) &&
            (compiler_options()->isolated_modules || compiler_options()->verbatim_module_syntax)
        ) {
            check_isolated_enum_member(me, names, errs);
        }
        check_isolated_global_shadow(me, names, errs);
        // (First declared by another file: another flow container, whose walk may not have run yet, maybe on another
        // thread)
        // (A signature without a body is a flow container of its own too, tsc's binder: its references have no flow
        // node here, see identifier_flow_type())
        my outer_reference = scope_flow(names) && (scope_flow(names)->member_name || symbol_first_declared_elsewhere(my target, names->file) ||
            walk_flow_start_differs(my target, scope_flow(names)->container_start) ||
            (scope_flow(names)->function && !scope_flow(names)->function->has_body && scope_flow(names)->container == scope_flow(names)->function &&
                !my target->is_parameter));
    } else if (span_equals(identifier_expr_name(me), "arguments") && scope_has_arguments(names)) {
        if (in_initializer_or_static_block(names)) {
            // (tsc's checkIdentifier: the error type)
            error_ctx_error_d(errs, identifier_expr_name(me), 2815, "'arguments' cannot be referenced in property initializers or class static initialization blocks.");
            my invalid_initializer_reference = true;
        }
        // No symbol: ty_identifier_reference_type() makes it IArguments
        my is_arguments = true;
        note_arguments_reference(names);
    } else if (scope_flow(names) && scope_flow(names)->type_only_alias_ok &&
        (my symbol = scope_resolve(names, span_sv(identifier_expr_name(me)), SYMBOL_ANY_MEANING)) &&
        // (`export = string`: a type keyword names nothing, TS2693 below)
        (!symbol_is_builtin_type(my symbol) || ((my symbol = nullptr), false))
    ) {
        // `export = N`, `export default N`: a name exports whatever it means (tsc's checkExportAssignment resolves it
        // with every meaning), a namespace or a type is fine. No target: it has no value type
    } else if (identifier_extends_entity(me) && entity_names_interface(names, identifier_extends_entity(me))) {
        // (tsc's checkAndReportErrorForExtendingInterface)
        error_ctx_error_d(errs, identifier_expr_name(me), 2689, "Cannot extend an interface '", expr_outer_span(identifier_extends_entity(me)),
            "'. Did you mean 'implements'?");
        my symbol = scope_resolve(names, span_sv(identifier_expr_name(me)), SYMBOL_ANY_MEANING);
        my namespace_as_value = identifier_extends_entity(me)->tag != EXPR_IDENTIFIER;
    } else if (scope_resolve(names, span_sv(identifier_expr_name(me)), SYMBOL_NAMESPACE_MODULE)) {
        // (tsc's checkAndReportErrorForUsingNamespaceAsTypeOrValue)
        error_ctx_error_d(errs, identifier_expr_name(me), 2708, "Cannot use namespace '", identifier_expr_name(me), "' as a value.");
        my symbol = scope_resolve(names, span_sv(identifier_expr_name(me)), SYMBOL_NAMESPACE_MODULE);
    } else if (scope_get_type(names, span_sv(identifier_expr_name(me)), nullptr) &&
        !scope_hides_base_expression_type_parameter(names, scope_resolve(names, span_sv(identifier_expr_name(me)), SYMBOL_TYPE)) &&
        // (A keyword type is no name tsc's resolveName finds: only any, string, number, boolean, never and unknown are
        // said to be types, checkAndReportErrorForUsingTypeAsValue's isPrimitiveTypeName; `object` isn't found)
        !(symbol_is_builtin_type(scope_get_type(names, span_sv(identifier_expr_name(me)), nullptr)) &&
            !is_value_primitive_type_name(span_sv(identifier_expr_name(me))))
    ) {
        // (tsc's isES2015OrLaterConstructorName)
        static char const* const later[] = { "Promise", "Symbol", "Map", "WeakMap", "Set", "WeakSet" };
        bool later_lib = false;
        for (u32 i = 0; i < c_arraylen(later); i++) later_lib |= csview_equals(span_sv(identifier_expr_name(me)), later[i]);
        if (later_lib) {
            error_ctx_error_d(errs, identifier_expr_name(me), 2585, "'", identifier_expr_name(me), "' only refers to a type, but is being used as a value "
                "here. Do you need to change your target library? Try changing the 'lib' compiler option to es2015 or later.");
        } else if (my class_base && is_primitive_type_name(span_sv(identifier_expr_name(me)))) {
            error_ctx_error_d(errs, identifier_expr_name(me), 2863, "A class cannot extend a primitive type like '", identifier_expr_name(me),
                "'. Classes can only extend constructable values.");
        } else {
            Expr const* lone_key = scope_rare(names)->lone_computed_key;
            bool maybe_mapped = lone_key && lone_key->tag == EXPR_IDENTIFIER && &lone_key->identifier == me;
            scope_report_type_as_value(names->binder, scope_resolve(names, span_sv(identifier_expr_name(me)), SYMBOL_TYPE),
                identifier_expr_name(me), maybe_mapped, errs);
        }
    } else if (my call_child && span_equals(identifier_expr_name(me), "await")) {
        // (tsc's getCannotFindNameDiagnosticForName)
        error_ctx_error_d(errs, identifier_expr_name(me), 2311, "Cannot find name 'await'. Did you mean to write this in an "
            "async function?");
    } else if (!my shorthand_unchecked) {
        scope_report_value_not_found(names, identifier_expr_name(me), my shorthand, errs);
    }
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
    if (!(symbol->flags & SYMBOL_VARIABLE) && !(span_source(identifier_expr_name(me)) && span_source(identifier_expr_name(me))->is_js && (symbol->flags & SYMBOL_VALUE_MODULE))) {
        char const* what = symbol->flags & SYMBOL_ENUM ? "an enum" : symbol->flags & SYMBOL_CLASS ? "a class" :
            symbol->flags & (SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE) ? "a namespace" :
            symbol->flags & SYMBOL_FUNCTION ? "a function" : symbol->flags & SYMBOL_ALIAS ? "an import" : nullptr;
        unsigned code = symbol->flags & SYMBOL_ENUM ? 2628 : symbol->flags & SYMBOL_CLASS ? 2629 :
            symbol->flags & (SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE) ? 2631 : symbol->flags & SYMBOL_FUNCTION ? 2630 :
            symbol->flags & SYMBOL_ALIAS ? 2632 : 2539;
        if (what) {
            error_ctx_error_d(errs, identifier_expr_name(me), code, "Cannot assign to '", identifier_expr_name(me), "' because it is ", what, ".");
        } else {
            error_ctx_error_d(errs, identifier_expr_name(me), code, "Cannot assign to '", identifier_expr_name(me), "' because it is not a variable.");
        }
        return true;
    }
    if (symbol->is_const) {
        error_ctx_error_d(errs, identifier_expr_name(me), 2588, "Cannot assign to '", identifier_expr_name(me), "' because it is a constant.");
        return true;
    }
    return false;
}

bool identifier_is_invalid_assignment_target(IdentifierExpr const* me) {
    Symbol const* symbol = my symbol;
    if (!my assignment_target || !symbol || !my target || symbol->is_placeholder || my keyword) return false;
    return !(symbol->flags & SYMBOL_VARIABLE) || symbol->is_const;
}

// tsc's IsDeprecatedDeclaration: a `@deprecated` JSDoc comment before the declaration's statement (the combined node
// flags of a variable declaration include its statement's), found by going back from the declaration over its
// keywords and modifiers
static bool declaration_deprecated(SymbolDecl const* decl) {
    Span at = span_valid(decl->full) ? decl->full : decl->name;
    SourceFile const* file = span_source(at);
    if (!file || !file->deprecated_jsdoc_count) return false;
    const char* begin = source_file_start(file);
    const char* p = at.buf;
    static const char* const keywords[] = { "export", "declare", "default", "const", "let", "var", "function", "class",
        "interface", "enum", "namespace", "module", "type", "abstract", "async", "using" };
    while (true) {
        while (p > begin && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r')) p--;
        const char* word = p;
        while (word > begin && (isalnum((unsigned char)word[-1]) || word[-1] == '_' || word[-1] == '$')) word--;
        bool keyword = false;
        for (u32 i = 0; i < c_arraylen(keywords) && !keyword && word < p; i++) {
            keyword = (size_t)(p - word) == strlen(keywords[i]) && !memcmp(word, keywords[i], (size_t)(p - word));
        }
        if (!keyword) break;
        p = word;
    }
    return source_file_deprecated_jsdoc_ends_at(file, p);
}

// tsc's isDeprecatedSymbol: its value declaration is deprecated, or every declaration is
static bool symbol_deprecated(Symbol const* symbol) {
    if (!symbol->decl_count) return false;
    bool every = true;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        bool deprecated = declaration_deprecated(&symbol->decls[i]);
        if (deprecated && (symbol->decls[i].flags & SYMBOL_VALUE)) return true;
        every &= deprecated;
    }
    return every;
}

void identifier_expr_typecheck(IdentifierExpr const* me, ErrorCtx* errs) {
    report_invalid_assignment_target(me, errs);
    // A reference to a deprecated declaration: a suggestion (tsc's checkIdentifier, addDeprecatedSuggestion). Not a
    // function's, which tsc decides by the call's signature (isUncalledFunctionReference, TS6387): not done
    if (compiler_options()->capture_suggestions && my target && !my keyword && !my target->is_placeholder &&
        !(my target->flags & (SYMBOL_FUNCTION | SYMBOL_METHOD)) && symbol_deprecated(my target)
    ) {
        Error* e = error_ctx_error_d(errs, identifier_expr_name(me), 6385, "'", identifier_expr_name(me), "' is deprecated.");
        if (e) e->suggestion = true;
    }
    IdentifierFlowIssue issue = ty_identifier_flow_issue(current_checker, me);
    switch (issue) {
        case IDENTIFIER_FLOW_OK: break;
        case IDENTIFIER_FLOW_UNASSIGNED: {
            // (symbolToString: the name as its declaration writes it, `\u0078x`)
            Span written = my target && span_valid(my target->name_span) && my target->name_span.size ? my target->name_span : identifier_expr_name(me);
            error_ctx_error_d(errs, identifier_expr_name(me), 2454, "Variable '", written, "' is used before being assigned.");
        }   break;
        case IDENTIFIER_FLOW_IMPLICIT_ANY:
        case IDENTIFIER_FLOW_IMPLICIT_ANY_ARRAY: {
            if (!compiler_options()->no_implicit_any) break;
            char const* t = issue == IDENTIFIER_FLOW_IMPLICIT_ANY ? "any" : "any[]";
            Binding const* binding = symbol_find_node(my target, SYMBOL_DECL_VARIABLE);
            error_ctx_error_d(errs, binding->content_span, 7034,
                "Variable '", identifier_expr_name(me), "' implicitly has type '", t, "' in some locations where its type cannot be determined.");
            error_ctx_error_d(errs, identifier_expr_name(me), 7005, "Variable '", identifier_expr_name(me), "' implicitly has an '", t, "' type.");
        }   break;
    }
    if (my keyword == KEYWORD_IDENTIFIER_NEW_TARGET) {
        // tsc's checkGrammarMetaProperty: the name after `new.`
        char const* end = span_end_ptr(identifier_expr_name(me));
        char const* start = end;
        while (start > span_start_ptr(identifier_expr_name(me)) && start[-1] != '.' && !isspace((unsigned char)start[-1])) start--;
        Span property = span_make_in(identifier_expr_name(me), start, end);
        if (!csview_equals(span_sv(property), "target")) {
            error_ctx_grammar_error_d(errs, property, 17012, "'", property,
                "' is not a valid meta-property for keyword 'new'. Did you mean 'target'?");
        }
    }
    if (my keyword == KEYWORD_IDENTIFIER_THIS && my this_kind == THIS_NAMESPACE) {
        error_ctx_error_d(errs, identifier_expr_name(me), 2331, "'this' cannot be referenced in a module or namespace body.");
        // (Nor does it have a type: tsc's tryGetThisTypeAt of a namespace)
        if (compiler_options()->no_implicit_this) {
            error_ctx_error_d(errs, identifier_expr_name(me), 2683, "'this' implicitly has type 'any' because it does not have a type annotation.");
        }
    }
    // (tsc's checkThisExpression: `typeof globalThis` through an arrow function)
    if (my keyword == KEYWORD_IDENTIFIER_THIS && my this_kind == THIS_GLOBAL && my this_through_arrow &&
        compiler_options()->no_implicit_this
    ) {
        error_ctx_error_d(errs, identifier_expr_name(me), 7041, "The containing arrow function captures the global value of 'this'.");
    }
    if (my keyword == KEYWORD_IDENTIFIER_THIS && my this_kind == THIS_ENUM) {
        error_ctx_error_d(errs, identifier_expr_name(me), 2332, "'this' cannot be referenced in current location.");
        // (Nor does it have a type: tsc's tryGetThisTypeAt of an enum is undefined)
        if (compiler_options()->no_implicit_this) {
            error_ctx_error_d(errs, identifier_expr_name(me), 2683, "'this' implicitly has type 'any' because it does not have a type annotation.");
        }
    }
    // `this` in a function nothing says the `this` of (tsc's checkThisExpression)
    if (my keyword == KEYWORD_IDENTIFIER_THIS && my this_kind == THIS_ANY && compiler_options()->no_implicit_this &&
        !(identifier_this_function(me) && ty_contextual_this_type(current_checker, identifier_this_function(me)))
    ) {
        Error* e = error_ctx_error_d(errs, identifier_expr_name(me), 2683, "'this' implicitly has type 'any' because it does not have a type annotation.");
        FunctionImpl const* outer = identifier_this_function(me) ? function_impl_rare(identifier_this_function(me))->outer_this_function : nullptr;
        if (identifier_this_function(me) && (my this_shadows_outer ||
            (outer && ty_contextual_this_type(current_checker, outer)))
        ) {
            error_add_related_d(e, function_error_span(identifier_this_function(me)), 2738, "An outer value of 'this' is shadowed by this container.");
        }
    }
}

// `f<A, B>` outside of a call (there the type arguments are the call's business, ty/call.c)
// tsc's getInstantiationExpressionType, as far as its diagnostics go: what signatures a type (a part of a union member)
// has (TS2635), and the constraints of each that takes the type arguments (TS2344, getInstantiatedSignatures)
typedef struct InstantiationScan {
    GenericParamsExpr const* expr; // Null: only TS2635
    ErrorCtx* errs;
    u32 written; // Type arguments
    bool has_signatures, has_applicable; // Of the union member being looked at
    bool some_applicable; // Of any
    Ty const* non_applicable; // The first union member with signatures and none that take the type arguments
} InstantiationScan;

static void instantiation_scan_type(Checker* checker, InstantiationScan* scan, Ty const* type, u32 depth);

// The first type argument that doesn't satisfy its constraint in `sig` (tsc's checkTypeArguments with reportErrors)
static bool check_type_argument_constraints(Checker* checker, GenericParamsExpr const* me, Signature const* sig, ErrorCtx* errs) {
    SigResolved const* sig_r = sig_res(checker, sig);
    for (u32 i = 0; i < sig_r->type_params.len && (isize_t)i < vec_spanned_type_size(&my params); i++) {
        Ty const* constraint = ty_type_argument_constraint(checker, sig_r->type_params, &my params, i);
        if (!constraint) continue;
        SpannedType const* arg = vec_spanned_type_at(&my params, i);
        // (Explained like any unassignable pair: tsc's checkTypeArgumentConstraints -> checkTypeAssignableTo, one run)
        Ty const* arg_type = ty_of_node(&arg->type);
        // (With `this` the argument, unlike a type reference's: checkTypeArguments' getTypeWithThisArgument)
        constraint = ty_with_this_arg(checker, constraint, arg_type);
        VecTyReportLine lines = {0};
        bool overflowed = false;
        if (ty_relate_explaining_at(checker, arg_type, constraint, 2344, &lines, errs, arg->span, &overflowed)) continue;
        if (overflowed) return false;
        // (The head names the target as isRelatedTo left it: `T | undefined` is T for a non-nullable argument)
        // (A literal argument is its base type only against a target without literals: tsc's reportRelationError,
        // `Pair<"y">` against `"x"`)
        cstr arg_str, constraint_str;
        ty_report_pair_cstr(checker, arg_type, ty_report_target(checker, arg_type, constraint), &arg_str, &constraint_str);
        cstr head = cstr_d("Type '", arg_str, "' does not satisfy the constraint '", constraint_str, "'.");
        c_drop(cstr, &arg_str, &constraint_str);
        ty_report_explained_with_head(errs, &lines, 2344, head, arg->span);
        return false;
    }
    return true;
}

static void instantiation_scan_part(Checker* checker, InstantiationScan* scan, Ty const* type, u32 depth) {
    if (type->kind == TY_UNION && !(type->flags & TYF_BOOLEAN)) {
        for (u32 i = 0; i < type->composite.types.len; i++) instantiation_scan_type(checker, scan, type->composite.types.data[i], depth + 1);
    } else if (type->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < type->composite.types.len; i++) instantiation_scan_part(checker, scan, type->composite.types.data[i], depth + 1);
    } else if (type->flags & TYF_OBJECT) {
        Members const* members = ty_members(checker, type);
        for (u32 i = 0; i < members->call_count + members->construct_count; i++) {
            Signature const* sig = i < members->call_count ? members->call[i] : members->construct[i - members->call_count];
            scan->has_signatures = true;
            SigResolved const* sig_rs = signature_resolve(checker, sig);
            u32 min = 0;
            for (u32 j = 0; j < sig_rs->type_params.len; j++) {
                if (!ty_param_default(checker, sig_rs->type_params.data[j])) min = j + 1;
            }
            // (An empty list, an import type's, takes any generic one: tsc's hasCorrectTypeArgumentArity)
            if (sig_rs->type_params.len && (!scan->written || (scan->written >= min && scan->written <= sig_rs->type_params.len))) {
                scan->has_applicable = true;
                if (scan->expr) check_type_argument_constraints(checker, scan->expr, sig, scan->errs);
            }
        }
    } else if (type->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) { // (tsc's InstantiableNonPrimitive)
        Ty const* constraint = ty_base_constraint(checker, type);
        if (constraint && constraint != type) instantiation_scan_part(checker, scan, constraint, depth + 1);
    }
}

static void instantiation_scan_type(Checker* checker, InstantiationScan* scan, Ty const* type, u32 depth) {
    bool outer_has = scan->has_signatures, outer_applicable = scan->has_applicable;
    scan->has_signatures = scan->has_applicable = false;
    instantiation_scan_part(checker, scan, type, depth);
    scan->some_applicable |= scan->has_applicable;
    if (scan->has_signatures && !scan->has_applicable && !scan->non_applicable) scan->non_applicable = type;
    scan->has_signatures = outer_has;
    scan->has_applicable = outer_applicable;
}

Ty const* instantiation_error_type(Checker* checker, Ty const* callee, u32 written) {
    if (callee == ty_intrinsic(checker, TY_ERROR)) return nullptr;
    InstantiationScan scan = { .written = written };
    instantiation_scan_type(checker, &scan, callee, 0);
    return scan.some_applicable ? scan.non_applicable : callee;
}

void generic_params_expr_typecheck(GenericParamsExpr* me, ErrorCtx* errs) {
    expr_typecheck(&my expr, errs);
    for_each (i, vec_spanned_type, &my params) {
        type_typecheck(&i->type, errs);
    }
    // (`f<>`: TS1099 only, tsc's getInstantiationExpressionType returns the type as it is without type arguments)
    if (vec_spanned_type_is_empty(&my params)) return;
    Checker* checker = current_checker;
    Ty const* callee = ty_expr_type(checker, &my expr);
    // (Only the error type has nothing to say: `any<T>` is TS2635 too, tsgo's getInstantiationExpressionType)
    if (callee == ty_intrinsic(checker, TY_ERROR) || ((callee->flags & TYF_ANY) && my heritage)) return;
    u32 written = (u32)vec_spanned_type_size(&my params);
    if (!my heritage) {
        InstantiationScan scan = { .expr = me, .errs = errs, .written = written };
        instantiation_scan_type(checker, &scan, callee, 0);
        Ty const* error_type = scan.some_applicable ? scan.non_applicable : callee;
        if (error_type) {
            error_ctx_error_d(errs, span_between(vec_spanned_type_at(&my params, 0)->span, vec_spanned_type_back(&my params)->span),
                2635, "Type '", error_type, "' has no signatures for which the type argument list is applicable.");
        }
        return;
    }
    // A base class expression: the constraints of each signature that takes this many type arguments up to the first
    // that fails (tsc's checkClassLikeDeclaration, getConstructorsForTypeArguments), else of the first signature
    Members const* members = ty_members(checker, callee);
    u32 total = members->call_count + members->construct_count;
    bool fits = false;
    for (u32 i = 0; i < total; i++) {
        Signature const* each = i < members->call_count ? members->call[i] : members->construct[i - members->call_count];
        SigResolved const* each_res = signature_resolve(checker, each);
        u32 min = 0;
        for (u32 j = 0; j < each_res->type_params.len; j++) {
            if (!ty_param_default(checker, each_res->type_params.data[j])) min = j + 1;
        }
        if (!each_res->type_params.len || written < min || written > each_res->type_params.len) continue;
        fits = true;
        if (!check_type_argument_constraints(checker, me, each, errs)) break;
    }
    if (!fits && total) {
        Signature const* first = members->call_count ? members->call[0] : members->construct[0];
        signature_resolve(checker, first);
        check_type_argument_constraints(checker, me, first, errs);
    }
}

void expr_drop(Expr* me) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
            if (my missing_decorators) {
                vec_expr_drop(my missing_decorators);
                ast_delete(my missing_decorators);
                my missing_decorators = nullptr;
            }
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
            ast_delete(my binary_op->access_site);
            if (my binary_op->jsdoc_type && !my binary_op->jsdoc_type_borrowed) {
                type_drop(my binary_op->jsdoc_type);
                ast_delete(my binary_op->jsdoc_type);
            }
            ast_delete(my binary_op);
            break;

        case EXPR_UNARY_OP:
            expr_drop(&my unary_op->expr);
            ast_delete(my unary_op);
            break;

        case EXPR_PROPERTY:
            expr_drop(&my property->expr);
            if (my property->form == PROPERTY_EXPR_BRACKET) expr_drop(&my property->index);
            ast_delete(my property->rare);
            ast_free(my property, my property->form == PROPERTY_EXPR_DOT ? property_dot_size : c_sizeof(PropertyExpr));
            break;

        case EXPR_GENERIC_PARAMS:
            expr_drop(&my generic_params->expr);
            vec_spanned_type_drop(&my generic_params->params);
            ast_delete(my generic_params);
            break;

        case EXPR_AS_TYPE:
            expr_drop(&my as_type->expr);
            type_drop(&my as_type->type);
            ast_delete(my as_type);
            break;

        case EXPR_TERNARY:
            expr_drop(&my ternary->condition);
            expr_drop(&my ternary->yes);
            expr_drop(&my ternary->no);
            ast_delete(my ternary);
            break;

        case EXPR_CALL:
            expr_drop(&my call->callable);
            vec_expr_drop(&my call->args);
            if (my call->rare) ast_delete(my call->rare->unique_symbol);
            ast_delete(my call->rare);
            ast_delete(my call);
            break;

        case EXPR_FUNCTION:
            function_impl_drop(my function);
            ast_delete(my function);
            break;

        case EXPR_OBJECT:
            vec_object_expr_item_drop(&my object);
            break;

        case EXPR_ARRAY:
            vec_expr_drop(&my array);
            break;

        case EXPR_CLASS_DECL:
            class_decl_drop(my class_decl);
            ast_delete(my class_decl);
            break;

        case EXPR_SPREAD:
            expr_drop(my spread);
            ast_delete(my spread);
            break;

        case EXPR_REGEX:
            break;

        case EXPR_PRIVATE_NAME:
            ast_delete(my private_name);
            break;

        case EXPR_TEMPLATE:
            if (my template_->tag) {
                expr_drop(my template_->tag);
                ast_delete(my template_->tag);
            }
            vec_expr_drop(&my template_->substitutions);
            ast_cstr_drop(&my template_->plain_string);
            ast_cstrs_drop(&my template_->texts);
            ast_delete(my template_);
            break;

        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT:
            jsx_element_drop(my jsx);
            ast_delete(my jsx);
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

static bool jsx_tag_is_intrinsic(Expr const* tag_name, bool namespaced) {
    if (namespaced) return true;
    if (tag_name->tag != EXPR_IDENTIFIER || tag_name->identifier.keyword) return false;
    csview name = span_sv(tag_name->inner_span);
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

bool jsx_element_is_intrinsic(JsxElement const* me) {
    return jsx_tag_is_intrinsic(&my tag_name, my namespaced);
}

bool jsx_closing_is_intrinsic(JsxElement const* me) {
    return jsx_tag_is_intrinsic(&my closing_name, my closing_namespaced);
}

// `x as const` / `<const>x`: `const` isn't a type name (the new core checks the same, see is_const_assertion())
static bool as_type_is_const_assertion(AsTypeExpr const* me) {
    return my type.tag == TYPE_IDENTIFIER && span_equals(my type.identifier->name, "const") &&
        !identifier_type_namespace_count(my type.identifier) && !vec_spanned_type_size(&my type.identifier->generic_params);
}

// Control flow (flow.h). These follow the functions of the same names in tsc's binder

static bool binary_op_is_logical(BinaryOp op) {
    return op == BINARY_OP_AND_AND || op == BINARY_OP_OR_OR || op == BINARY_OP_EH_EH;
}

static bool binary_op_is_logical_assignment(BinaryOp op) {
    return op == BINARY_OP_AND_AND_EQUAL || op == BINARY_OP_OR_OR_EQUAL || op == BINARY_OP_EH_EH_EQUAL;
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
        // (tsc's checkAwaitGrammar: the containing function, if it isn't async already)
        if (e && function && !function->is_constructor && !function->is_async) {
            error_add_related_d(e, function_error_span(function), 1356, "Did you mean to mark this function as 'async'?");
        }
        return;
    }
    // (An `await` expression's TS18037 is the parser's; tsc's checkGrammarForInOrForOfStatement for `for await`)
    if (place == AWAIT_IN_STATIC_BLOCK && kind == AWAIT_KIND_FOR) {
        error_ctx_grammar_error_d(errs, at, 18038, "'for await' loops cannot be used inside a class static block.");
        return;
    }
    if (place != AWAIT_AT_TOP_LEVEL) return;
    SourceFile const* file = span_source(at);
    CompilerOptions const* options = compiler_options();
    if (!file->is_module) {
        error_ctx_grammar_error_d(errs, at, kind == AWAIT_KIND_EXPR ? 1375 : kind == AWAIT_KIND_FOR ? 1431 : 2853,
            what[kind], " are only allowed at the top level of a file when that file is a module, but this file has "
            "no imports or exports. Consider adding an empty 'export {}' to make this file a module.");
    }
    bool node = options->module == MODULE_KIND_NODE16 || options->module == MODULE_KIND_NODE18 || options->module == MODULE_KIND_NODE20 ||
        options->module == MODULE_KIND_NODENEXT;
    bool es = options->module == MODULE_KIND_ES2022 || options->module == MODULE_KIND_ESNEXT ||
        options->module == MODULE_KIND_PRESERVE;
    if (node && source_file_implied_format_for_emit(file) == MODULE_KIND_COMMONJS) {
        error_ctx_grammar_error_d(errs, at, 1309, "The current file is a CommonJS module and cannot use 'await' at the top level.");
    } else if (!(node || es) || options->target < SCRIPT_TARGET_ES2017) {
        error_ctx_grammar_error_d(errs, at, kind == AWAIT_KIND_EXPR ? 1378 : kind == AWAIT_KIND_FOR ? 1432 : 2854,
            "Top-level ", what[kind], " are only allowed when the 'module' option is set to 'es2022', 'esnext', 'system', "
            "'node16', 'node18', 'node20', 'nodenext', or 'preserve', and the 'target' option is set to 'es2017' or higher.");
    }
}
static bool is_written_undefined(Expr const* me);


void expr_lookup_names_condition(Expr* me, Scope* names, ErrorCtx* errs, FlowNode* true_target, FlowNode* false_target) {
    FlowBuilder* flow = scope_flow(names);
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

void expr_mark_result_unused(Expr* me) {
    if (me->tag == EXPR_UNARY_OP && (me->unary_op->op == UNARY_OP_YIELD || me->unary_op->op == UNARY_OP_YIELD_STAR)) {
        me->unary_op->result_unused = true;
    } else if (me->tag == EXPR_BINARY_OP && me->binary_op->op == BINARY_OP_COMMA) {
        expr_mark_result_unused(&me->binary_op->right); // (the left one is whatever the comma is in)
    }
}

void expr_lookup_names_statement(Expr* me, Scope* names, ErrorCtx* errs) {
    expr_mark_result_unused(me);
    if (my tag == EXPR_CALL) my call->is_statement = true;
    // (The context root of the function expressions in it: the walk checks it without a contextual type)
    uintptr_t outer_root = scope_flow(names)->context_root;
    scope_flow(names)->context_root = context_root_make(me, CONTEXT_ROOT_EXPR);
    expr_lookup_names(me, names, errs);
    scope_flow(names)->context_root = outer_root;
    flow_maybe_call(scope_flow(names), me);
}

static void flow_assignment_target(FlowBuilder* flow, Expr const* target, FlowAssign assign);

// The `this.x` targets of a TypeScript destructuring assignment (`({ a: this.a } = e)`, `[this.b] = e`) assign their
// fields like `this.x = v` does (see scope_note_ts_this_assignment())
static void note_pattern_this_assignments(Scope const* names, Expr const* target) {
    if (target->tag == EXPR_BINARY_OP && target->binary_op->op == BINARY_OP_EQUAL) target = &target->binary_op->left;
    if (target->tag == EXPR_SPREAD) target = target->spread;
    if (target->tag == EXPR_PROPERTY) {
        scope_note_ts_this_assignment(names, target);
    } else if (target->tag == EXPR_ARRAY) {
        for_each (i, vec_expr, &target->array) note_pattern_this_assignments(names, i);
    } else if (target->tag == EXPR_OBJECT) {
        for_each (i, vec_object_expr_item, &target->object) {
            if (i->tag == OBJECT_ITEM_PROPERTY || i->tag == OBJECT_ITEM_SPREAD) note_pattern_this_assignments(names, &i->value);
        }
    }
}

// An element of an assignment pattern, which may have a default: `[a = 1] = e`
static void flow_destructuring_target(FlowBuilder* flow, Expr const* target, FlowAssign assign, FlowPatternPath path) {
    if (target->tag == EXPR_BINARY_OP && target->binary_op->op == BINARY_OP_EQUAL) {
        path.default_value = &target->binary_op->right;
        path.plain_default = path.parent != nullptr;
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
                    path.default_value = object_item_rare(i)->shorthand_default; // `{ a = 1 } = e`
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
    flow_assignment_target(scope_flow(names), me, (FlowAssign){ .kind = FLOW_ASSIGN_FOR_EACH, .for_each = for_each });
}

void expr_link_assignment_declaration(Expr* me, Binder* binder) {
    if (my tag != EXPR_PROPERTY || !property_rare(my property)->declares || my property->form != PROPERTY_EXPR_DOT) return;
    // (The declaration this assignment made: tsc's getControlFlowContainer of a valueDeclaration)
    Symbol* declared = (Symbol*)property_rare(my property)->declares;
    for (u32 d = 0; d < declared->decl_count; d++) {
        SymbolDecl* decl = &declared->decls[d];
        if (decl->kind == SYMBOL_DECL_ASSIGNMENT && span_start_ptr(decl->name) == span_start_ptr(expr_outer_span(me)) &&
            span_source(decl->name) == span_source(expr_outer_span(me))
        ) {
            if (decl->js) decl->js->flow_container = my property->context->flow_container;
            // (Files resolved on other threads may set it too)
            __atomic_store_n(&binder->has_assignment_flow_containers, true, __ATOMIC_RELAXED);
        }
    }
}

// What narrowing inside closures needs to know about a local variable (tsc's markNodeAssignments())
static void identifier_mark_assigned(IdentifierExpr const* me, Scope const* names) {
    Symbol* symbol = (Symbol*)my target; // Local: this file's walk owns it
    if (!symbol || !symbol->is_local || !(symbol->flags & SYMBOL_VARIABLE)) return;
    if (scope_flow(names)->unmarked_assignments) return;
    for (Scope const* i = names; i && !scope_declares_symbol(i, symbol); i = i->parent) {
        if (i->kind == SCOPE_ENUM) return;
    }
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
    char const* end = span_end_ptr(identifier_expr_name(me));
    char const* declared_at = span_start_ptr(symbol->name_span);
    for (FlowStatementFrame const* i = scope_flow(names)->statements; i; i = i->parent) {
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
    FlowBuilder* flow = scope_flow(names);
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
    FlowBuilder* flow = scope_flow(names);
    bool is_root = expr_is_optional_chain_root(me);
    FlowNode* pre_chain = is_root ? flow_new_branch_label(flow) : nullptr;
    Expr* object =
        my tag == EXPR_PROPERTY ? &my property->expr :
        my tag == EXPR_CALL ? &my call->callable : &my unary_op->expr;
    flow->true_target = pre_chain ? pre_chain : true_target;
    flow->false_target = false_target;
    flow->chain_continues = !is_root;
    flow->callee_of_call = my tag == EXPR_CALL;
    expr_lookup_names(object, names, errs);
    if (is_root) {
        // (When the object is a chain of its own it has added the same already: so does tsc)
        flow_add_nullish_conditions(flow, object, pre_chain, false_target);
        flow_set_current(flow, flow_finish_label(pre_chain));
    }
    if (my tag == EXPR_PROPERTY) {
        if (my property->form == PROPERTY_EXPR_BRACKET) expr_lookup_names(&my property->index, names, errs);
        else if (!my property->name_slot) my property->name_slot = lazy_slot_new();
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
    FlowBuilder* flow = scope_flow(names);
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
        !csview_equals(span_sv(left->inner_span), "0")
    ) {
        return;
    }
    bool eval = right->tag == EXPR_IDENTIFIER && !right->identifier.keyword && csview_equals(span_sv(right->inner_span), "eval");
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
// there is none. What there is to say about it the checker says (jsx_report_runtime_module())
static Symbol* jsx_runtime_module(Scope* names) {
    ModuleRef* ref = names->file ? names->file->jsx_runtime_ref : nullptr;
    return ref ? binder_module_of_ref(names->binder, ref) : nullptr;
}

void jsx_report_runtime_module(SourceFile const* file, Span at, ErrorCtx* errs) {
    ModuleRef* ref = file ? file->jsx_runtime_ref : nullptr;
    if (!ref) return;
    if (ref->module) {
        // (tsc's resolveExternalModule, with the element as the error node)
        module_ref_report_require_of_esm(ref, at, errs);
        return;
    }
    if (!ref->ambient_only && ref->failure == MODULE_REF_IS_JS) {
        module_ref_report_implicit_any_at(ref, at, errs); // (tsc's resolveExternalModule)
        return;
    }
    if (!ref->ambient_only && ref->failure == MODULE_REF_NOT_BUILT) {
        // (A referenced project's unbuilt source, like an import's: tsc's resolveExternalModule finds the redirect)
        module_ref_report_not_built_at(ref, at, errs);
        return;
    }
    if (!ref->ambient_only && ref->failure != MODULE_REF_NOT_FOUND) return;
    error_ctx_error_d(errs, at, 2875, "This JSX tag requires the module path '", ref->path, "' to exist, but none "
        "could be found. Make sure you have types for the appropriate package installed.");
}

static Symbol* jsx_reference_factory(JsxElement const* el, bool fragment, csview name, Scope* names, ErrorCtx* errs) {
    JsxKind jsx = compiler_options()->jsx;
    u32 meaning = jsx == JSX_PRESERVE || jsx == JSX_REACT_NATIVE ? SYMBOL_VALUE & ~(u32)SYMBOL_ENUM : SYMBOL_VALUE;
    Symbol* found = scope_resolve(names, name, meaning);
    Span at = fragment || !el->tag_name.inner_span.size ? el->opening_span : el->tag_name.inner_span;
    if (found) {
        unused_mark_referenced(names, found, SYMBOL_VALUE);
        // (The factory is a name like any other: tsc's resolveName)
        // (resolveName's checks of what it found run only with a not-found message, which is jsx: react's; a type-only
        // one only at an expression: a tag name, not a fragment nor `a:b`)
        if (jsx == JSX_REACT) check_umd_global_symbol_use(found, at, names, errs);
        if (jsx == JSX_REACT && !fragment && el->tag_name.inner_span.size && !el->namespaced) {
            check_type_only_value_at(found, at, name, names, errs);
        }
    } else if (jsx == JSX_REACT && scope_resolve(names, name, SYMBOL_NAMESPACE_MODULE)) {
        // (tsc's resolveName -> checkAndReportErrorForUsingNamespaceAsValue, before any suggestion)
        error_ctx_error_d(errs, at, 2708, "Cannot use namespace '", name, "' as a value.");
    } else if (jsx == JSX_REACT) {
        // (tsc's resolveName: a misspelling is said to be one instead)
        scope_report_jsx_factory_not_found(names, at, name, meaning, errs);
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
    el->runtime_module = jsx_runtime_module(names);
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
// tsc's checkJsxExpression: `{a, b}` in an attribute or child (a parenthesized one is fine)
static void jsx_check_comma_expression(Expr const* expr, ErrorCtx* errs) {
    if (expr->tag != EXPR_BINARY_OP || expr->binary_op->op != BINARY_OP_COMMA || expr_is_perenthisized(expr)) return;
    error_ctx_grammar_error_d(errs, expr_outer_span(expr), 18007,
        "JSX expressions may not use the comma operator. Did you mean to write an array?");
}

static void jsx_check_children_grammar(JsxElement const* el, ErrorCtx* errs) {
    for_each (i, vec_jsx_child, &el->children) {
        if (i->tag == JSX_CHILD_EXPR) jsx_check_comma_expression(&i->expr, errs);
    }
}

static void jsx_check_attributes_grammar(JsxElement const* el, ErrorCtx* errs) {
    VecJsxAttribute const* attributes = &el->attributes.jsx_attributes;
    for_each (i, vec_jsx_attribute, attributes) {
        if (i->tag == JSX_ATTRIBUTE_EXPR) jsx_check_comma_expression(&i->value, errs);
    }
    for (isize i = 0; i < vec_jsx_attribute_size(attributes); i++) {
        JsxAttribute const* attr = vec_jsx_attribute_at(attributes, i);
        if (attr->tag == JSX_ATTRIBUTE_SPREAD) continue;
        for (isize j = 0; j < i; j++) {
            JsxAttribute const* earlier = vec_jsx_attribute_at(attributes, j);
            if (earlier->tag != JSX_ATTRIBUTE_SPREAD && csview_equals_sv(span_sv(earlier->name), span_sv(attr->name))) {
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
            jsx_check_children_grammar(my jsx, errs);
            // A value tag (`<Comp />`, `<this.x />`) is a reference, an intrinsic one (`<div />`) is a name
            // (a missing name, after a parse error, is nothing)
            bool intrinsic = my tag == EXPR_JSX_FRAGMENT || jsx_element_is_intrinsic(my jsx) ||
                !my jsx->tag_name.inner_span.size;
            if (!intrinsic) {
                expr_lookup_names(&my jsx->tag_name, names, errs);
            } else if (my tag == EXPR_JSX_ELEMENT && !my jsx->namespaced) {
                // Only for the types baseline, which shows an intrinsic tag name as a value of that name (tsc's
                // getTypeAtLocation checks it as an expression): no reference
                // (With its flow node, tsc's binder gives every identifier expression one: a `var` assigned later is
                // undefined there)
                IdentifierExpr* name = &my jsx->tag_name.identifier;
                if (!name->keyword) {
                    name->target = scope_get_value(names, span_sv(identifier_expr_name(name)), &name->symbol);
                    name->flow = flow_builder_reference(scope_flow(names), my jsx->tag_name.inner_span);
                }
            }
            for_each (i, vec_spanned_type, &my jsx->type_args) type_lookup_names(&i->type, names, errs);
            expr_lookup_names(&my jsx->attributes, names, errs);
            for_each (i, vec_jsx_child, &my jsx->children) {
                if (i->tag == JSX_CHILD_EXPR || i->tag == JSX_CHILD_ELEMENT) expr_lookup_names(&i->expr, names, errs);
            }
            // (The closing tag by its own name: tsc's checkJsxElementDeferred)
            if (my tag == EXPR_JSX_ELEMENT && my jsx->closing_name.tag != EXPR_IMPLICIT_ANY &&
                span_valid(my jsx->closing_name.inner_span) && my jsx->closing_name.inner_span.size
            ) {
                IdentifierExpr* closing = &my jsx->closing_name.identifier;
                if (!jsx_closing_is_intrinsic(my jsx)) {
                    expr_lookup_names(&my jsx->closing_name, names, errs);
                } else if (!my jsx->closing_namespaced && my jsx->closing_name.tag == EXPR_IDENTIFIER && !closing->keyword) {
                    closing->target = scope_get_value(names, span_sv(identifier_expr_name(closing)), &closing->symbol);
                    closing->flow = flow_builder_reference(scope_flow(names), my jsx->closing_name.inner_span);
                }
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
    FlowBuilder* flow = scope_flow(names);
    if (my binary_op->op == BINARY_OP_COMMA) expr_mark_result_unused(&my binary_op->left);
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

AccessSite* scope_access_site(Scope* names) {
    ClassDecl const* enclosing_class = scope_enclosing_class(names);
    ThisRef this_ref = scope_get_this(names, false);
    Symbol const* this_parameter = this_ref.kind == THIS_PARAMETER ? this_ref.symbol : nullptr;
    FunctionImpl const* this_function = this_ref.kind == THIS_ANY ? this_ref.function : nullptr;
    if (!enclosing_class && !this_parameter && !this_function) return nullptr;
    // (The AST's: a deferred body's goes with its materialization)
    return ast_new(AccessSite, {
        .enclosing_class = enclosing_class, .containing_function = names->function, .this_parameter = this_parameter,
        .this_function = this_function, .initializer_context = scope_flow(names) ? scope_flow(names)->initializer_context : 0,
    });
}

void expr_lookup_names(Expr* me, Scope* names, ErrorCtx* errs) {
    lazy_type_bind(&my lazy_type, LAZY_TYPE_EXPR);

    // What this expression is to whoever walks it: taken, so that operands only get what they are given
    FlowBuilder* flow = scope_flow(names);
    FlowNode* true_target = flow->true_target;
    FlowNode* false_target = flow->false_target;
    bool chain_continues = flow->chain_continues;
    uint8_t assignment_target = flow->assignment_target;
    bool compound_like_target = flow->compound_like_target;
    flow->compound_like_target = false;
    bool read_target = flow->read_target;
    flow->read_target = false;
    bool nested_read_target = flow->nested_read_target;
    flow->nested_read_target = false;
    bool nullish_condition = flow->nullish_condition;
    flow->nullish_condition = false;
    bool callee_of_call = flow->callee_of_call;
    flow->callee_of_call = false;
    flow->true_target = nullptr;
    flow->false_target = nullptr;
    flow->chain_continues = false;
    flow->assignment_target = FLOW_TARGET_NONE;
    // (Only object and array literals, spreads and prefix `-`/`+` pass it on to their operands, see EXPR_AS_TYPE)
    bool const_context = flow->const_context;
    flow->const_context = const_context && (my tag == EXPR_OBJECT || my tag == EXPR_ARRAY || my tag == EXPR_SPREAD ||
        (my tag == EXPR_UNARY_OP && (my unary_op->op == UNARY_OP_MINUS || my unary_op->op == UNARY_OP_PLUS)));

    // `undefined` is a name: a declaration of it (`var undefined` in a module, an import, a script's `class undefined`,
    // which tsc's addUndefinedToGlobalsOrErrorOnRedeclaration keeps as the global) hides tsc's undefinedSymbol
    if (is_written_undefined(me)) {
        Symbol const* declared = scope_resolve(names, span_sv(my inner_span), SYMBOL_VALUE);
        if (declared && !declared->is_placeholder) {
            Expr old = *me;
            *me = expr_identifier(old.inner_span, (IdentifierExpr){0});
            my paren_before = old.paren_before;
            my paren_after = old.paren_after;
            my lazy_type = old.lazy_type;
        }
    }

    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
            if (my missing_decorators) {
                for_each (i, vec_expr, my missing_decorators) expr_lookup_names(i, names, nullptr);
            }
            break;
        case EXPR_LITERAL:
            my undefined_assigned = assignment_target != FLOW_TARGET_NONE && is_written_undefined(me);
            if (my literal.tag == VALUE_BIGINT) {
                bool ambient = false;
                for (Scope const* i = names; i && !ambient; i = i->parent) ambient = i->ambient;
                // (`declare const n = 1n`)
                Binding const* initializing = scope_flow(names) && scope_flow(names)->initializing ? &scope_flow(names)->initializing->binding : nullptr;
                if (initializing && initializing->tag == BINDING_IDENTIFIER && initializing->symbol) ambient |= initializing->symbol->is_ambient;
                my literal_ambient = ambient;
            }
            break;

        case EXPR_IDENTIFIER:
            identifier_expr_lookup_names(&my identifier, names, errs);
            // `x = 1` doesn't read x (tsc's isWriteOnlyAccess)
            if (assignment_target != FLOW_TARGET_DEFINITE || read_target) {
                unused_mark_referenced(names, (Symbol*)my identifier.symbol, SYMBOL_VALUE);
            }
            my identifier.flow = flow_builder_reference(flow, my inner_span);
            my identifier.assignment_target = assignment_target;
            my identifier.compound_like_target = compound_like_target;
            my identifier.assume_initialized = flow->in_type_query && !flow->export_equals_name;
            // (tsc's isInAmbientOrTypeNode: `declare class C { [k]: T }`)
            for (Scope const* i = names; i && !my identifier.assume_initialized; i = i->parent) {
                my identifier.assume_initialized = i->ambient;
            }
            if (flow->binding_root && my identifier.target) {
                // `const { a = "0", b = +a } = obj`
                Binding const* declared = symbol_find_node(my identifier.target, SYMBOL_DECL_VARIABLE);
                while (declared && declared->parent) declared = declared->parent;
                if (declared == flow->binding_root) my identifier.assume_initialized = true;
            }
            if (assignment_target) identifier_mark_assigned(&my identifier, names);
            if (!my identifier.keyword) {
                scope_check_strict_identifier(names, my inner_span, flow->in_type_query);
                if (assignment_target) scope_check_strict_eval_or_arguments(names, my inner_span);
            }
            break;

        case EXPR_BINARY_OP: {
            BinaryOp op = my binary_op->op;
            Expr* left = &my binary_op->left;
            Expr* right = &my binary_op->right;
            if (my binary_op->jsdoc_type && !my binary_op->jsdoc_type_borrowed) type_lookup_names(my binary_op->jsdoc_type, names, errs);
            if (op == BINARY_OP_EQUAL && left->tag == EXPR_OBJECT) {
                ast_delete(my binary_op->access_site);
                my binary_op->access_site = scope_access_site(names);
            }
            // `o.m = function () {}` (or `??=` & co.): `this` in there may be `o`
            if (binary_op_is_assignment(op) && right->tag == EXPR_FUNCTION && !function_impl_is_arrow(right->function) &&
                left->tag == EXPR_PROPERTY
            ) {
                function_impl_rare_mut(right->function)->this_assigned_object = &left->property->expr;
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
                    if (names->file && !names->file->is_js) note_pattern_this_assignments(names, left);
                }
            } else if (binary_op_is_assignment(op)) {
                // `a = 1` as an element of a pattern is a target with a default: whoever walks the pattern assigns
                flow->assignment_target = op == BINARY_OP_EQUAL ? FLOW_TARGET_DEFINITE : FLOW_TARGET_COMPOUND;
                flow->compound_like_target = op == BINARY_OP_EQUAL && is_compound_like(right);
                expr_lookup_names(left, names, errs);
                if (op == BINARY_OP_EQUAL) scope_add_nested_expando(names, my binary_op);
                if (op == BINARY_OP_EQUAL && names->file && names->file->is_js) scope_add_this_property(names, my binary_op);
                if (op == BINARY_OP_EQUAL && names->file) scope_note_ts_this_assignment(names, left);
                expr_lookup_names(right, names, errs);
                if (!assignment_target) {
                    flow_assignment_target(flow, left, (FlowAssign){ .kind = FLOW_ASSIGN_EXPR, .assignment = me });
                    flow_array_mutation(flow, me);
                }
            } else {
                plain_binary_lookup_names(me, names, errs);
            }
        }   break;

        case EXPR_UNARY_OP: {
            Expr* operand = &my unary_op->expr;
            if (my unary_op->op == UNARY_OP_VOID) expr_mark_result_unused(operand);
            // tsc's checkStrictModeDeleteExpression
            // (A missing operand is a missing identifier to tsc)
            if (my unary_op->op == UNARY_OP_DELETE && names->strict && !expr_is_perenthisized(operand) &&
                ((operand->tag == EXPR_IDENTIFIER && !operand->identifier.keyword) ||
                    (operand->tag == EXPR_IMPLICIT_ANY && !operand->inner_span.size) || is_written_undefined(operand))
            ) {
                error_ctx_error_d(errs, expr_error_span_skip_parens(operand), 1102, "'delete' cannot be called on an identifier in strict mode.");
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
                        flow->nested_read_target = assignment_target != FLOW_TARGET_NONE;
                        expr_lookup_names(operand, names, errs);
                        // `x!` says x was assigned, whatever control flow thinks (not `(x)!`: tsc asks whether the
                        // identifier's parent is the NonNullExpression, isNonNullExpression(node.Parent))
                        if (operand->tag == EXPR_IDENTIFIER && !expr_is_perenthisized(operand)) {
                            operand->identifier.assume_initialized = true;
                            operand->identifier.non_null_operand = true;
                        } else if (operand->tag == EXPR_PROPERTY && !operand->paren_before) {
                            operand->property->non_null_operand = true;
                        }
                    }
                    break;

                case UNARY_OP_AWAIT:
                    // tsc's checkAwaitGrammar: reported whether or not the file has parse errors
                    if (my unary_op->await_place == AWAIT_IN_STATIC_BLOCK) {
                        error_ctx_error_d(errs, my inner_span, 18037, "'await' expression cannot be used inside a class static block.");
                    }
                    await_check_grammar(my unary_op->await_place, AWAIT_KIND_EXPR,
                        span_in_sv(my inner_span, csview_from_n(my inner_span.buf, 5)),
                        scope_containing_function(names), errs);
                    expr_lookup_names(operand, names, errs);
                    break;

                case UNARY_OP_YIELD:
                case UNARY_OP_YIELD_STAR:
                    if (my unary_op->outside_generator) {
                        // tsc's checkYieldExpressionGrammar; tsgo's checkYieldExpression checks the operand anyway
                        Span keyword = span_in_sv(my inner_span, csview_from_n(my inner_span.buf, 5));
                        error_ctx_grammar_error_d(errs, keyword, 1163, "A 'yield' expression is only allowed in a generator body.");
                    }
                    expr_lookup_names(operand, names, errs);
                    if (flow->function && flow->function->is_generator && !my unary_op->outside_generator && !flow->method_name) {
                        my unary_op->generator = flow->function;
                        my unary_op->no_type_params_in_scope = !names->generics;
                    }
                    // (A class field's in a generator is TS1163, but still something the generator yields)
                    FlowBuilder* generator_flow = yield_generator_body_flow(names);
                    if (generator_flow) {
                        if (generator_flow->yield_count == generator_flow->yield_cap) {
                            generator_flow->yield_cap = generator_flow->yield_cap ? generator_flow->yield_cap * 2 : 4;
                            generator_flow->yields = realloc(generator_flow->yields, sizeof(Expr const*) * generator_flow->yield_cap);
                            panic_if(!generator_flow->yields, "out of memory");
                        }
                        generator_flow->yields[generator_flow->yield_count++] = me;
                    }
                    break;

                default:
                    expr_lookup_names(operand, names, errs);
                    break;
            }
        }   break;

        case EXPR_PROPERTY:
            // Also when it can't be narrowed (`f().x`): the checker finds out when it makes the reference key
            my property->flow = flow_builder_reference(flow, expr_outer_span(me));
            flow_length_access(me);
            my property->assignment_target = assignment_target;
            my property->read_target = read_target;
            my property->nested_read_target = nested_read_target;
            my property->compound_like_target = compound_like_target;
            {
                PropertyContext context = { .enclosing_class = scope_enclosing_class(names) };
                ThisRef this_ref = scope_get_this(names, false);
                if (this_ref.kind == THIS_PARAMETER) context.this_parameter = this_ref.symbol;
                if (this_ref.kind == THIS_ANY) context.this_function = this_ref.function;
                context.in_constructor_of = flow->constructor_of;
                if (my property->form == PROPERTY_EXPR_DOT && my property->expr.tag != EXPR_PROPERTY) {
                    // (Not inside `typeof this.x` written as a type: tsc's isInPropertyInitializerOrClassStaticBlock quits
                    // at a TypeQuery)
                    context.initializer_context = flow->in_type_query ? INITIALIZER_NONE : flow->initializer_context;
                    context.initializer_field = flow->initializer_field;
                }
                context.containing_function = names->function;
                context.flow_container = flow->container;
                my property->context = flow_builder_property_context(flow, &context);
            }
            if (my property->form == PROPERTY_EXPR_DOT && span_valid(property_name(my property)) &&
                property_name(my property).buf[0] == '#'
            ) {
                csview name = span_sv(property_name(my property));
                ClassDecl const* c = my property->context->enclosing_class;
                while (c && !class_decl_find_private_name(c, name)) c = c->outer_class;
                if (c || my property->rare) property_rare_mut(my property)->private_class_ = c;
            }
            expr_link_assignment_declaration(me, names->binder);
            if (my property->expr.tag == EXPR_IDENTIFIER) {
                my property->expr.identifier.super_member = my property->expr.identifier.keyword == KEYWORD_IDENTIFIER_SUPER;
            }
            // (tsc's isConstraintPosition goes by the parent node: `(x).a`'s x is a parenthesized expression's)
            if (expr_is_perenthisized(&my property->expr)) {
            } else if (my property->form == PROPERTY_EXPR_DOT || my property->index.tag == EXPR_LITERAL) {
                if (my property->expr.tag == EXPR_IDENTIFIER) my property->expr.identifier.constraint_position = true;
                if (my property->expr.tag == EXPR_PROPERTY) my property->expr.property->constraint_position = true;
            } else if (my property->expr.tag == EXPR_IDENTIFIER && !my property->expr.identifier.keyword &&
                !my property->expr.identifier.require_call
            ) {
                // (Decided once the index is typed, see identifier_element_index(). TODO: an access's object, `a.b[i]`)
                IdentifierExpr* object = &my property->expr.identifier;
                object->constraint_position = true;
                object->element_object = true;
                object->evolving_index_ = &my property->index;
            }
            if (expr_is_optional_chain(me)) {
                expr_lookup_names_chain_flow(me, names, errs, true_target, false_target, chain_continues, nullish_condition);
            } else {
                expr_lookup_names(&my property->expr, names, errs);
                if (my property->form == PROPERTY_EXPR_BRACKET) expr_lookup_names(&my property->index, names, errs);
                else if (!my property->name_slot) my property->name_slot = lazy_slot_new();
            }
            if (my property->form == PROPERTY_EXPR_BRACKET && my property->index.tag == EXPR_IDENTIFIER &&
                my property->index.identifier.target
            ) {
                ForEachLoopStatement const* loop = scope_for_in_loop_of(names, my property->index.identifier.target);
                if (loop || my property->rare) property_rare_mut(my property)->for_in_loop_ = loop;
            }
            break;

        case EXPR_GENERIC_PARAMS:
            // `f<T>` is part of the call as far as flow goes
            flow->true_target = true_target;
            flow->false_target = false_target;
            flow->chain_continues = chain_continues;
            // (An instantiation expression is a type node to tsc, ExpressionWithTypeArguments, unlike a call's callee)
            if (!callee_of_call) flow->unmarked_assignments++;
            flow->callee_of_call = callee_of_call;
            expr_lookup_names(&my generic_params->expr, names, errs);
            if (!callee_of_call) flow->unmarked_assignments--;
            // (`super<T>` is TS2754, its type arguments never checked: tsc says nothing about what they name)
            bool super_args = my generic_params->expr.tag == EXPR_IDENTIFIER &&
                my generic_params->expr.identifier.keyword == KEYWORD_IDENTIFIER_SUPER;
            ErrorCtx quiet = errs ? error_ctx_scratch(errs) : (ErrorCtx){0};
            for_each (i, vec_spanned_type, &my generic_params->params) {
                type_lookup_names(&i->type, names, super_args && errs ? &quiet : errs);
            }
            error_ctx_drop(&quiet);
            break;

        case EXPR_AS_TYPE:
            // (A type assertion or `satisfies` types its operand contextually to tsgo's isContextuallyTyped, `as const`
            // makes it a const context)
            if (as_type_is_const_assertion(my as_type)) flow->const_context = true;
            else flow->contextual_depth++;
            expr_lookup_names(&my as_type->expr, names, errs);
            if (!as_type_is_const_assertion(my as_type)) flow->contextual_depth--;
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
            ClassDecl const* enclosing_class = my call->uses_new ? scope_enclosing_class(names) : nullptr;
            if (enclosing_class) call_rare_mut(my call)->enclosing_class = enclosing_class;
            // `import("m", { with: { type: "json" } })`: the attributes are a const context (tsgo's isConstContext,
            // isInlineImportAttributes: the `with` property's own object literal, the options parenthesized or not)
            if (my call->callable.tag == EXPR_IDENTIFIER && my call->callable.identifier.keyword == KEYWORD_IDENTIFIER_IMPORT &&
                vec_expr_size(&my call->args) > 1 && vec_expr_at(&my call->args, 1)->tag == EXPR_OBJECT
            ) {
                for_each (i, vec_object_expr_item, &vec_expr_at_mut(&my call->args, 1)->object) {
                    if (i->tag == OBJECT_ITEM_PROPERTY && !i->computed_key && cstr_equals(&i->name_string, "with") &&
                        i->value.tag == EXPR_OBJECT && !expr_is_perenthisized(&i->value)
                    ) {
                        i->inline_import_attributes = true;
                    }
                }
            }
            Expr const* constructor = my call->callable.tag == EXPR_GENERIC_PARAMS ? &my call->callable.generic_params->expr : &my call->callable;
            if (my call->uses_new && constructor->tag == EXPR_IDENTIFIER) {
                for_each (arg, vec_expr, &my call->args) {
                    if (arg->tag == EXPR_FUNCTION && !expr_is_perenthisized(arg)) function_impl_rare_mut(arg->function)->new_callee = constructor;
                }
            }
            Expr* callee = &my call->callable;
            while (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
            // (tsc's isConstraintPosition: the callee of any call, an optional one too)
            if (callee->tag == EXPR_IDENTIFIER && !expr_is_perenthisized(callee)) callee->identifier.constraint_position = true;
            if (!my call->uses_new) {
                if (callee->tag == EXPR_IDENTIFIER && !expr_is_perenthisized(callee)) callee->identifier.call_child = true;
                for_each (i, vec_expr, &my call->args) {
                    if (i->tag == EXPR_IDENTIFIER && !expr_is_perenthisized(i)) i->identifier.call_child = true;
                }
            }
            if (callee->tag == EXPR_PROPERTY && !expr_is_perenthisized(callee)) callee->property->constraint_position = true;
            if (expr_is_optional_chain(me)) {
                expr_lookup_names_chain_flow(me, names, errs, true_target, false_target, chain_continues, nullish_condition);
                flow_array_mutation(flow, me); // `x?.push(v)`
                break;
            }
            if (callee->tag == EXPR_PROPERTY) callee->property->method_call = true;
            if (callee->tag == EXPR_FUNCTION && !my call->uses_new) {
                // An immediately invoked function expression is part of this flow, after its arguments
                for_each (i, vec_expr, &my call->args) {
                    expr_lookup_names(i, names, errs);
                }
                flow->next_function_is_iife = !callee->function->is_async && !callee->function->is_generator;
                callee->function->is_iife = true;
                function_impl_rare_mut(callee->function)->iife_arg_count = (u32)vec_expr_size(&my call->args);
                function_impl_rare_mut(callee->function)->iife_call = my call;
                flow_call_argument_targets(my call);
                flow->callee_of_call = true;
                expr_lookup_names(&my call->callable, names, errs);
                flow->next_function_is_iife = false;
                break;
            }
            if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER && !my call->uses_new) {
                callee->identifier.super_call = true;
                // (tsc's findFirstSuperCall looks in the body only)
                bool in_method_name = false;
                for (Scope const* s = names; s && s->kind == SCOPE_BLOCK && !in_method_name; s = s->parent) {
                    in_method_name = s->no_super == NO_SUPER_LITERAL_METHOD_COMPUTED_NAME;
                }
                if (names->function && names->function->is_constructor && !function_impl_rare(names->function)->first_super_call_ &&
                    names->no_super != NO_SUPER_CONSTRUCTOR_ARGS && !in_method_name
                ) {
                    function_impl_rare_mut(names->function)->first_super_call_ = me;
                }
            } else if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER) {
                // `new super()` isn't a super call: super is the base instance like in `super.x` (tsc's
                // checkSuperExpression, isCallExpression)
                callee->identifier.super_member = true;
            }
            flow->callee_of_call = true;
            expr_lookup_names(&my call->callable, names, errs);
            for_each (i, vec_expr, &my call->args) {
                expr_lookup_names(i, names, errs);
            }
            if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER &&
                !my call->uses_new && !flow->dead
            ) {
                flow->current = flow_new_call(flow, flow->current, me);
            }
            flow_array_mutation(flow, me);
        }   break;

        case EXPR_FUNCTION: {
            // A function expression's name is only visible inside it
            Scope* name_scope = names;
            if (span_valid(my function->name)) {
                name_scope = scope_new(names, SCOPE_BLOCK);
                scope_rare_mut(name_scope)->expression_name_of = my function;
                function_impl_add_names(my function, name_scope, (DeclStatementProps){0}, false);
            }
            my function->syntactically_contextual = flow->contextual_depth > 0;
            // Where its contextual signature comes from (FunctionImpl.context_root), which its unannotated parameters
            // settle when asked for first (InitializedBindingRare.context_function)
            if (flow->context_root) {
                my function->context_root = flow->context_root;
                for_each (param, vec_function_arg, &my function->args) {
                    if (!param->initializer.binding.has_explicit_type) initialized_binding_rare_mut(&param->initializer)->context_function = my function;
                }
            }
            function_impl_lookup_names_as(my function, FUNCTION_IS_EXPRESSION, !function_impl_is_arrow(me->function), name_scope, errs);
        }   break;

        case EXPR_OBJECT:
            my assignment_pattern = assignment_target != FLOW_TARGET_NONE;
            my no_enclosing_type_params = !names->generics && !names->function && !(names->file && names->file->is_js) &&
                !scope_enclosing_class(names);
            // (Not in JavaScript, where a literal is an open-ended expando container that its instances don't stand for:
            // instantiating it by `this` lost that, jsObjectsMarkedAsOpenEnded)
            bool js_literal = names->file && names->file->is_js;
            my object_generics = my no_enclosing_type_params || js_literal ? nullptr : names->generics;
            my object_class = my no_enclosing_type_params || js_literal ? nullptr : scope_enclosing_class(names);
            // tsc's checkGrammarObjectLiteralExpression in a destructuring: a rest property is no pattern itself
            for_each (i, vec_object_expr_item, &my object) {
                if (!my assignment_pattern || i->tag != OBJECT_ITEM_SPREAD) continue;
                if (i->value.tag != EXPR_OBJECT && i->value.tag != EXPR_ARRAY) continue;
                error_ctx_grammar_error_d(errs, expr_outer_span(&i->value), 2501, "A rest element cannot contain a binding pattern.");
                break;
            }
            flow->object_literal_depth++;
            for_each (i, vec_object_expr_item, &my object) {
                if (i->computed_key) {
                    // (A scope of its own only for resolve_super())
                    Scope* key_scope = scope_new(names, SCOPE_BLOCK);
                    // (A method's or accessor's name is in it to tsc's getContainingFunction: TS1308's related information)
                    if (i->tag != OBJECT_ITEM_PROPERTY && i->value.tag == EXPR_FUNCTION) {
                        scope_rare_mut(key_scope)->decorated_function = i->value.function;
                    }
                    key_scope->no_super = i->tag != OBJECT_ITEM_PROPERTY || (i->value.tag == EXPR_FUNCTION && i->value.function->is_method) ?
                        NO_SUPER_LITERAL_METHOD_COMPUTED_NAME : NO_SUPER_LITERAL_COMPUTED_NAME;
                    // The name of a method or accessor is deferred like it, as far as a class initializer is concerned
                    // (TS2729), unless it reads the field it is in
                    uint8_t initializer_context = flow->initializer_context;
                    bool member_name = flow->member_name, method_name = flow->method_name;
                    if (i->tag != OBJECT_ITEM_PROPERTY) {
                        bool in_field = initializer_context == INITIALIZER_INSTANCE_FIELD || initializer_context == INITIALIZER_STATIC_FIELD;
                        flow->initializer_context = in_field ? INITIALIZER_MEMBER_NAME : INITIALIZER_NONE;
                        flow->member_name = flow->method_name = true;
                    }
                    FlowNode* flow_before = flow->current;
                    expr_lookup_names(i->computed_key, key_scope, errs);
                    // (A method's or accessor's name is bound in its own flow container, see lookup_names_without_super())
                    if (i->tag != OBJECT_ITEM_PROPERTY) flow->current = flow_before;
                    flow->initializer_context = initializer_context;
                    flow->member_name = member_name;
                    flow->method_name = method_name;
                }
                if (i->value.tag == EXPR_FUNCTION) {
                    i->value.function->accessor = i->tag == OBJECT_ITEM_GETTER ? 1 : i->tag == OBJECT_ITEM_SETTER ? 2 : 0;
                    if (i->tag != OBJECT_ITEM_PROPERTY) function_impl_set_member_name(i->value.function, i->name_span);
                    // (`a: () => {}` is named by the property: tsc's getAssignedName)
                    else if (!expr_is_perenthisized(&i->value)) function_impl_set_variable_name(i->value.function, i->name_span);
                    // tsc's getContainingObjectLiteral: methods, accessors, `a: function () {}` (not arrows)
                    if (!function_impl_is_arrow(i->value.function)) {
                        function_impl_rare_mut(i->value.function)->this_literal = me;
                        if (expr_is_perenthisized(&i->value)) function_impl_rare_mut(i->value.function)->this_literal_parenthesized = true;
                    }
                }
                // (A computed name pairs when late bound, an entity name or a literal, by its text like a class's)
                Expr const* own_key = i->computed_key;
                if (i->tag == OBJECT_ITEM_SETTER && (!own_key || own_key->tag == EXPR_LITERAL || expr_is_entity_name(own_key))) {
                    for_each (other, vec_object_expr_item, &my object) {
                        if (other->tag == OBJECT_ITEM_GETTER && !other->computed_key == !own_key && (own_key ?
                            csview_equals_sv(span_sv(expr_outer_span(other->computed_key)), span_sv(expr_outer_span(own_key))) :
                            cstr_eq(&other->name_string, &i->name_string))
                        ) {
                            function_impl_pair_setter(i->value.function, other->value.function, own_key);
                            break;
                        }
                    }
                }
                flow->assignment_target = assignment_target; // A pattern: the values are the targets
                i->pseudo_contextual = flow->contextual_depth > 0;
                i->pseudo_const = flow->const_context;
                if (object_item_is_private_name(i)) i->private_name_in_class = scope_enclosing_class(names) != nullptr;
                if (object_item_rare(i)->jsdoc_type) type_lookup_names(object_item_rare(i)->jsdoc_type, names, errs);
                if (object_item_rare(i)->shorthand_default && assignment_target == FLOW_TARGET_NONE && i->value.tag == EXPR_IDENTIFIER) {
                    i->value.identifier.shorthand_unchecked = true;
                }
                // (A property with a JSDoc type has a type annotation to tsgo's reparser: its value is contextually typed,
                // isContextuallyTyped)
                flow->contextual_depth += object_item_rare(i)->jsdoc_type != nullptr;
                expr_lookup_names(&i->value, names, errs);
                flow->contextual_depth -= object_item_rare(i)->jsdoc_type != nullptr;
                if (object_item_rare(i)->shorthand_default) expr_lookup_names_initializer(object_item_rare(i)->shorthand_default, names, errs);
            }
            flow->object_literal_depth--;
            break;

        case EXPR_ARRAY:
            my assignment_pattern = assignment_target != FLOW_TARGET_NONE;
            for_each (i, vec_expr, &my array) {
                flow->assignment_target = assignment_target;
                if (i->tag == EXPR_TEMPLATE && i->template_->tag && !expr_is_perenthisized(i)) i->template_->array_element = true;
                expr_lookup_names(i, names, errs);
            }
            break;

        case EXPR_CLASS_DECL: {
            Scope* class_name_scope = my class_decl->name_scope;
            if (class_name_scope) scope_reparent(class_name_scope, names);
            if (!class_name_scope) {
                class_name_scope = scope_new(names, SCOPE_BLOCK);
                class_decl_add_names(my class_decl, class_name_scope, (DeclStatementProps){0});
            }
            class_decl_lookup_names(my class_decl, class_name_scope, errs);
        }   break;

        case EXPR_SPREAD:
            flow->assignment_target = assignment_target;
            flow->read_target = true;
            flow->nested_read_target = assignment_target != FLOW_TARGET_NONE;
            expr_lookup_names(my spread, names, errs);
            break;

        case EXPR_REGEX:
            break;

        case EXPR_PRIVATE_NAME: {
            ClassDecl const* c = scope_enclosing_class(names);
            my private_name->enclosing_class = c;
            while (c && !class_decl_find_private_name(c, span_sv(my inner_span))) c = c->outer_class;
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
            // (what is in an element's braces is contextually typed to tsc's isContextuallyTyped: JsxExpression)
            flow->contextual_depth++;
            jsx_lookup_names(me, names, errs);
            flow->contextual_depth--;
            break;
    }
    if (my tag == EXPR_CALL && !my call->uses_new) flow->contextual_depth--;
    flow->const_context = const_context;
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

Expr const* expr_effective_check_node(Expr const* me) {
    while (my tag == EXPR_AS_TYPE && my as_type->is_satisfies) me = &my as_type->expr;
    return me;
}

// `target = source` where target is a reference or a destructuring pattern (tsc's checkReferenceAssignment and
// checkDestructuringAssignment): each reference in it gets what it destructures to. Null `source` when that can't be
// told (TODO: rest elements, iterables that aren't arrays), which checks the shape of the target only
// `source_expr`: the right-hand side of a plain assignment, which a failure is elaborated into
static void assignment_target_typecheck(
    Expr const* target, Ty const* source, Expr const* source_expr, AccessSite const* site, ErrorCtx* errs
);
static Expr const* skip_outer_expressions(Expr const* me);

void expr_destructuring_typecheck(Expr const* target, Ty const* source, ErrorCtx* errs) {
    assignment_target_typecheck(target, source, nullptr, nullptr, errs);
}

// JavaScript `ns.a = {}` where `ns.a.b = v` makes `ns.a` a namespace: an assignment declaration that isn't checked
// against what it declares, which is necessarily incomplete (tsc's isAssignmentDeclaration in checkBinaryLikeExpression)
static bool is_container_declaration(BinaryOpExpr const* op) {
    SourceFile const* file = span_source(op->left.inner_span);
    if (op->op != BINARY_OP_EQUAL || !file || !file->is_js || op->left.tag != EXPR_PROPERTY) return false;
    Expr const* init = expando_initializer((Expr*)&op->right, true, false);
    if (!init || init->tag != EXPR_OBJECT) return false;
    // (Only if it does declare members: `symbol.exports.size`. Unknown for CommonJS exports, taken as yes; an
    // assignment that declares nothing is none: `foo["baz"] = {}` of an annotated `foo`)
    Symbol const* declared = property_rare(op->left.property)->declares;
    if (!declared) return binary_commonjs_declaration(op, nullptr) != COMMONJS_TARGET_NONE;
    return declared->exports && declared->exports->count;
}

// The optional chain part of tsc's checkReferenceExpression: `a?.b = 1` (through assertions). Reports it with the
// message given, false if so
// The other part of tsc's checkReferenceExpression: an identifier or a property access (through assertions)
bool expr_check_reference_is_reference(Expr const* target, unsigned code, char const* message, ErrorCtx* errs) {
    Expr const* reference = skip_outer_expressions(target);
    if (((reference->tag != EXPR_IDENTIFIER || reference->identifier.keyword) && reference->tag != EXPR_PROPERTY) ||
        ty_is_import_meta(reference)
    ) {
        error_ctx_error_d(errs, expr_outer_span(target), code, message);
        return false;
    }
    return true;
}

bool expr_check_reference_not_optional(Expr const* target, unsigned code, char const* message, ErrorCtx* errs) {
    Expr const* reference = skip_outer_expressions(target);
    if (reference->tag != EXPR_PROPERTY || !expr_is_optional_chain(reference)) return true;
    error_ctx_error_d(errs, expr_outer_span(target), code, message);
    return false;
}

static void assignment_target_typecheck_of(
    Expr const* target, Ty const* source, Expr const* source_expr, AccessSite const* site, bool object_rest, Span at, ErrorCtx* errs
);

static void assignment_target_typecheck(
    Expr const* target, Ty const* source, Expr const* source_expr, AccessSite const* site, ErrorCtx* errs
) {
    assignment_target_typecheck_of(target, source, source_expr, site, false, span_null, errs);
}

// `rest`: the target of an object rest (`{ ...target } = o`), which is said to be so (tsc's checkReferenceAssignment).
// `at`: where a source that doesn't fit is reported, if not at the target (`(x!) = v`, `(x satisfies T) = v`: the
// whole left side, tsc's checkBinaryLikeExpression reports at it)
static void assignment_target_typecheck_of(
    Expr const* target, Ty const* source, Expr const* source_expr, AccessSite const* site, bool object_rest, Span at, ErrorCtx* errs
) {
    Checker* checker = current_checker;
    Span report_at = span_valid(at) ? at : expr_outer_span(target);
    if (!expr_check_reference_not_optional(target, 2779,
        "The left-hand side of an assignment expression may not be an optional property access.", errs)
    ) {
        return;
    }
    // (Any fits everything but never: tsc's isSimpleTypeRelatedTo. `x1[k] = v as any` with a never write type is TS2322)
    // (Not an object pattern: its rest element is `{}` of any, tsc's getRestType, which can miss properties)
    if (source && (source->flags & TYF_ANY) && !(source->flags & TYF_ERROR) && target->tag != EXPR_PROPERTY && target->tag != EXPR_IDENTIFIER &&
        target->tag != EXPR_OBJECT
    ) {
        source = nullptr;
    }
    // (A missing operand is tsc's missing identifier: a reference to nothing, not reported)
    if (target->tag == EXPR_IMPLICIT_ANY && target->missing) return;
    switch (target->tag) {
        case EXPR_LITERAL:
            // `undefined = x`: tsc's undefinedSymbol, which isn't a variable
            if (is_written_undefined(target) && !expr_is_perenthisized(target)) {
                error_ctx_error_d(errs, target->inner_span, 2539, "Cannot assign to 'undefined' because it is not a variable.");
                return;
            }
            break;

        case EXPR_IDENTIFIER:
            if (target->identifier.keyword == KEYWORD_IDENTIFIER_THIS) break; // (no variable: TS2364)
            // (Not assignable at all: said by identifier_expr_typecheck(), tsc's errorType)
            if (identifier_is_invalid_assignment_target(&target->identifier)) return;
            if (source && target->identifier.target) {
                Ty const* declared = ty_type_of_symbol(checker, target->identifier.target);
                if (target->identifier.compound_like_target) declared = ty_base_type_of_literal(checker, declared);
                // A compound assignment (`x += 1`) assigns to what the reference reads: its narrowed type, widened
                // (tsc's checkIdentifier with a compound assignmentKind, getBaseTypeOfLiteralType(flowType))
                if (target->identifier.assignment_target == FLOW_TARGET_COMPOUND) declared = ty_expr_type(checker, target);
                ty_check_assignable_expr(checker, source, declared, report_at, source_expr, errs);
            } else if (source && target->identifier.is_arguments) {
                // `arguments = x` in a function: the arguments object (IArguments)
                ty_check_assignable_expr(checker, source, ty_expr_type(checker, target), report_at, source_expr, errs);
            }
            return;

        case EXPR_PROPERTY: {
            if (ty_is_import_meta(target)) break; // A meta-property is no reference (TS2364)
            if (!source) return;
            Ty const* target_type = ty_expr_type(checker, target);
            // (A compound or logical assignment's direct target is checked against the setter's type: tsc's
            // checkAssignmentOperator, `a.r ??= v`; not one through a `!` or a spread, `a.r! = v`)
            if (target->property->assignment_target == FLOW_TARGET_COMPOUND ||
                (target->property->read_target && !target->property->nested_read_target)
            ) {
                target_type = ty_compound_assignee_type(checker, target, target_type);
            }
            ty_check_assignable_to_access(checker, source, target_type, report_at, source_expr, target, errs);
        }   return;

        case EXPR_UNARY_OP:
            // `x! = 1`
            if (target->unary_op->op != UNARY_OP_RIGHT_BANG) break;
            assignment_target_typecheck_of(&target->unary_op->expr, source, source_expr, site, false, report_at, errs);
            return;

        case EXPR_AS_TYPE:
            // `(x as T) = 1`: T is what's assigned to; `(x satisfies T) = 1` assigns to x
            if (target->as_type->is_satisfies) {
                assignment_target_typecheck_of(&target->as_type->expr, source, source_expr, site, false, report_at, errs);
            } else if (source) {
                ty_check_assignable_expr(checker, source, ty_expr_type(checker, target), report_at, source_expr, errs);
            }
            return;

        case EXPR_BINARY_OP:
            // `[x = 1] = a`: a default, which the walk checks as an assignment of its own
            if (target->binary_op->op != BINARY_OP_EQUAL || expr_is_perenthisized(target)) break;
            assignment_target_typecheck(
                &target->binary_op->left, source ? ty_without_undefined(checker, source) : nullptr, nullptr, site, errs
            );
            return;

        case EXPR_ARRAY: {
            if (expr_is_perenthisized(target)) break;
            bool array_like = source && !(source->flags & TYF_NEVER) && ty_extends_array(checker, source);
            // What isn't an array has to be iterable (tsc's checkArrayLiteralAssignment)
            Ty const* iterated = source && !array_like ?
                ty_iterated_type(checker, ITERATION_DESTRUCTURING, source, expr_outer_span(target), errs) : nullptr;
            u32 index = 0;
            for_each (i, vec_expr, &target->array) {
                Ty const* element = iterated;
                if (i->tag == EXPR_SPREAD && i_index + 1 < vec_expr_size(&target->array)) {
                    error_ctx_error_d(errs, expr_outer_span(i), 2462, "A rest element must be last in a destructuring pattern.");
                } else if (i->tag == EXPR_SPREAD) {
                    // (tsc's checkArrayLiteralDestructuringElementAssignment: an initializer at its `=`, else a trailing comma)
                    Expr const* rest_target = i->spread;
                    if (rest_target->tag == EXPR_BINARY_OP && rest_target->binary_op->op == BINARY_OP_EQUAL &&
                        !expr_is_perenthisized(rest_target)
                    ) {
                        char const* at = span_end_ptr(expr_outer_span(&rest_target->binary_op->left));
                        char const* end = span_start_ptr(expr_outer_span(&rest_target->binary_op->right));
                        while (at < end && *at != '=') at++;
                        if (at < end) {
                            error_ctx_error_d(errs, span_make_in(target->inner_span, at, at + 1), 1186,
                                "A rest element cannot have an initializer.");
                        }
                    } else {
                        Span comma = span_comma_after(expr_outer_span(i));
                        if (span_valid(comma)) {
                            error_ctx_grammar_error_d(errs, comma, 1013, "A rest parameter or binding pattern may not have a trailing comma.");
                        }
                    }
                    // (With an initializer TS1186 is all: tsc's checkArrayLiteralDestructuringElementAssignment returns)
                    if (rest_target->tag == EXPR_BINARY_OP && rest_target->binary_op->op == BINARY_OP_EQUAL &&
                        !expr_is_perenthisized(rest_target)
                    ) {
                        index++;
                        continue;
                    }
                    // The rest of a tuple, else an array of the elements (tsc's checkArrayLiteralDestructuringElementAssignment)
                    Ty const* rest = nullptr;
                    if (source) {
                        rest = ty_tuple_rest_slice(checker, source, index);
                        if (!rest) {
                            if (array_like) element = ty_access(checker, source, ty_intrinsic(checker, TY_NUMBER));
                            rest = element ? ty_array(checker, element, false) : nullptr;
                        }
                    }
                    assignment_target_typecheck(i->spread, rest, nullptr, site, errs);
                } else if (i->tag == EXPR_LITERAL && expr_outer_span(i).size == 0) {
                    // A hole
                } else {
                    // Past the end of a tuple (tsc's checkArrayLiteralDestructuringElementAssignment: TS2493, TS2339 for
                    // a union of tuples), unless there's a default
                    bool has_default = i->tag == EXPR_BINARY_OP && i->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(i);
                    bool past_end = false;
                    if (array_like && !has_default) {
                        Value key = value_number(index);
                        ErrorCtx found = error_ctx_scratch(errs);
                        ty_check_property_access(checker, source, ty_literal(checker, &key), span_null, expr_outer_span(i), false,
                            false, &found);
                        past_end = !error_ctx_empty(&found);
                        error_ctx_merge(errs, &found);
                        error_ctx_drop(&found);
                    }
                    if (past_end) {
                        element = ty_intrinsic(checker, TY_ERROR); // (tsc: getIndexedAccessTypeOrUndefined() || errorType)
                    } else if (array_like) {
                        Value key = value_number(index);
                        // (Read like an expression: noUncheckedIndexedAccess's undefined)
                        element = ty_access_reading(checker, source, ty_literal(checker, &key));
                        if (!element) element = ty_access_reading(checker, source, ty_intrinsic(checker, TY_NUMBER));
                        // (Narrowed like the reference `source[index]` would be there: tsc's getFlowTypeOfDestructuring)
                        Expr const* reference = source_expr ? skip_outer_expressions(source_expr) : nullptr;
                        if (element && reference && (reference->tag == EXPR_IDENTIFIER || reference->tag == EXPR_PROPERTY)) {
                            element = ty_destructured_assignment_flow_type(checker, i, reference, key, expr_outer_span(i), element);
                        }
                    }
                    if (!array_like) element = ty_possibly_out_of_bounds(checker, element);
                    assignment_target_typecheck(i, element, nullptr, site, errs);
                }
                index++;
            }
            return;
        }

        case EXPR_OBJECT:
            if (expr_is_perenthisized(target)) break;
            // `{} = v`: v must not be null or undefined (tsc's checkObjectLiteralAssignment)
            if (source && !vec_object_expr_item_size(&target->object)) {
                ty_check_non_null_non_void_at(checker, source, target->inner_span, false, errs);
            }
            // (tsc's isThisInitializedObjectBindingExpression)
            bool from_this = source_expr && source_expr->tag == EXPR_IDENTIFIER &&
                source_expr->identifier.keyword == KEYWORD_IDENTIFIER_THIS;
            for_each (i, vec_object_expr_item, &target->object) {
                Ty const* property = nullptr;
                if (source && object_item_is_private_name(i)) {
                    // (A never key, TS18064: tsgo's getLiteralTypeFromPropertyName of a private identifier)
                    property = ty_intrinsic(checker, TY_NEVER);
                } else if (source && (i->tag == OBJECT_ITEM_PROPERTY || i->tag == OBJECT_ITEM_SHORTHAND) && !i->computed_key) {
                    Ty const* key = ty_string_literal(checker, cstr_sv(&i->name_string));
                    // (undefined and null have no properties: a union with them has none either, tsc's
                    // getIndexedAccessType on the source as it is)
                    bool nullable_member = false;
                    if (compiler_options()->strict_null_checks && source->kind == TY_UNION && !(source->flags & TYF_BOOLEAN)) {
                        for (u32 m = 0; m < source->composite.types.len; m++) {
                            nullable_member |= (source->composite.types.data[m]->flags & TYF_NULLABLE) != 0;
                        }
                    }
                    property = nullable_member ? nullptr : ty_access_reading(checker, source, key);
                    // (Narrowed like the reference `source.name` would be there: tsc's getFlowTypeOfDestructuring)
                    Expr const* reference = source_expr ? skip_outer_expressions(source_expr) : nullptr;
                    if (property && reference && (reference->tag == EXPR_IDENTIFIER || reference->tag == EXPR_PROPERTY)) {
                        Value name = { .tag = VALUE_STRING, .string = i->name_string };
                        property = ty_destructured_assignment_flow_type(checker, i, reference, name, i->name_span, property);
                    }
                    ty_check_destructured_member(checker, source, key, i->name_span, from_this, true, site, errs);
                    // What the source lacks is TS2339, on the source as it is: null and undefined too (tsc's
                    // checkObjectLiteralDestructuringPropertyAssignment). An object literal source may lack a property
                    // that has a default (AccessFlags.AllowMissing)
                    bool has_default = object_item_rare(i)->shorthand_default ||
                        (i->value.tag == EXPR_BINARY_OP && i->value.binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(&i->value));
                    if (!property && !(has_default && source->kind == TY_ANONYMOUS && source->anonymous.object_literal)) {
                        ty_check_property_access(checker, source, key, span_null, i->name_span, false, true, errs);
                    }
                }
                if (source && (i->tag == OBJECT_ITEM_PROPERTY || i->tag == OBJECT_ITEM_SHORTHAND) && i->computed_key) {
                    // (tsc's checkObjectLiteralDestructuringPropertyAssignment: getIndexedAccessType with the name as
                    // the access node)
                    // (With a default, an object literal may lack it: AccessFlags.AllowMissing)
                    bool has_default = object_item_rare(i)->shorthand_default ||
                        (i->value.tag == EXPR_BINARY_OP && i->value.binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(&i->value));
                    Ty const* key = ty_regular(checker, ty_expr_type(checker, i->computed_key));
                    Span key_span = expr_outer_span(i->computed_key);
                    if (!(has_default && source->kind == TY_ANONYMOUS && source->anonymous.object_literal)) {
                        ty_check_destructuring_indexed_access(checker, source, key, key_span, errs);
                    }
                    ty_check_destructured_member(checker, source, key, i->name_span, from_this, true, site, errs);
                    property = ty_access_reading(checker, source, key);
                }
                if (i->tag == OBJECT_ITEM_SPREAD && i_index + 1 < vec_object_expr_item_size(&target->object)) {
                    error_ctx_error_d(errs, i->span, 2462, "A rest element must be last in a destructuring pattern.");
                } else if (i->tag == OBJECT_ITEM_SPREAD) {
                    // (tsc's checkObjectLiteralDestructuringPropertyAssignment)
                    Span comma = span_comma_after(i->span);
                    if (span_valid(comma)) {
                        error_ctx_grammar_error_d(errs, comma, 1013, "A rest parameter or binding pattern may not have a trailing comma.");
                    }
                    if (expr_check_reference_not_optional(&i->value, 2778,
                        "The target of an object rest assignment may not be an optional property access.", errs)
                    ) {
                        // The rest of the source, without what the other properties name (tsc's
                        // checkObjectLiteralDestructuringPropertyAssignment with getRestType)
                        Ty const* rest = nullptr;
                        if (source) {
                            u32 count = (u32)vec_object_expr_item_size(&target->object);
                            Ty const** keys = malloc(sizeof(Ty const*) * (count ? count : 1));
                            panic_if(!keys, "out of memory");
                            u32 len = 0;
                            for_each (other, vec_object_expr_item, &target->object) {
                                if (other->tag == OBJECT_ITEM_SPREAD) continue;
                                keys[len++] = other->computed_key ? ty_regular(checker, ty_expr_type(checker, other->computed_key)) :
                                    ty_string_literal(checker, cstr_sv(&other->name_string));
                            }
                            // (Of any or the error type: tsc's getRestType makes an object of its properties, none: `{}`.
                            // A variable declaration's rest stays any, getBindingElementTypeFromParentType returns first)
                            rest = (source->flags & TYF_ANY) ? ty_object_literal(checker, nullptr, 0, nullptr, 0, OBJECT_SYNTHETIC) :
                                ty_object_rest_type(checker, source, (TyList){ .data = keys, .len = len });
                            free(keys);
                        }
                        assignment_target_typecheck_of(&i->value, rest, nullptr, site, true, span_null, errs);
                    }
                } else if (i->tag == OBJECT_ITEM_SHORTHAND && object_item_rare(i)->shorthand_default) {
                    // `{ a = 1 } = o`: the default is assigned to `a` (checked with a's type as its contextual type),
                    // and so is the property, without undefined unless the default can be undefined too (tsc's
                    // checkDestructuringAssignment of a ShorthandPropertyAssignment)
                    Ty const* declared = ty_expr_type(checker, &i->value);
                    Ty const* default_type = ty_check_expr(checker, object_item_rare(i)->shorthand_default, declared);
                    expr_typecheck(object_item_rare(i)->shorthand_default, errs);
                    assignment_target_typecheck(&i->value, default_type, object_item_rare(i)->shorthand_default, site, errs);
                    if (property && compiler_options()->strict_null_checks &&
                        !(default_type->flags & (TYF_ANY | TYF_UNKNOWN)) &&
                        ty_without_undefined(checker, default_type) == default_type
                    ) {
                        property = ty_without_undefined(checker, property);
                    }
                    assignment_target_typecheck(&i->value, property, nullptr, site, errs);
                } else if (i->tag == OBJECT_ITEM_PROPERTY || i->tag == OBJECT_ITEM_SHORTHAND) {
                    assignment_target_typecheck(&i->value, property, nullptr, site, errs);
                }
            }
            return;

        default:
            break;
    }
    if (object_rest) {
        error_ctx_error_d(errs, expr_outer_span(target), 2701,
            "The target of an object rest assignment must be a variable or a property access.");
    } else {
        error_ctx_error_d(errs, expr_outer_span(target), 2364,
            "The left-hand side of an assignment expression must be a variable or a property access.");
    }
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
    return my tag == EXPR_LITERAL && my literal.tag == VALUE_UNDEFINED && csview_equals(span_sv(my inner_span), "undefined");
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
    return expr_is_perenthisized(me) ? expr_outer_span(me) : node_error_span(me);
}

Span expr_error_span_skip_parens(Expr const* me) {
    return node_error_span(me);
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
            if (function_impl_is_arrow(function)) {
                // Only the first line of a block body that spans several
                char const* end = span_end_ptr(span);
                if (function->expression_body) return span;
                char const* line_end = span_end_ptr(function_impl_arrow_span(function));
                while (line_end < end && *line_end != '\n' && *line_end != '\r') line_end++;
                return line_end < end ? span_make_in(span, start, line_end) : span;
            }
            if (span_valid(function->name)) return function->name;
            break;
        }
        case EXPR_CLASS_DECL:
            if (span_valid(my class_decl->name)) return my class_decl->name;
            break;
        case EXPR_IMPLICIT_ANY: {
            // A missing expression is reported at its full start, before the trivia (tsc's getErrorSpanForNode:
            // nodeIsMissing). Only whitespace is walked back over
            if (!my missing || !span_valid(span) || !span_source(span)) return span;
            char const* file_start = cstr_str(&span_source(span)->contents);
            while (start > file_start && (start[-1] == ' ' || start[-1] == '\t' || start[-1] == '\n' || start[-1] == '\r')) start--;
            return span_make_in(span, start, start);
        }
        default:
            return span;
    }
    // The first token (`function`, `async` or `class`)
    char const* end = start;
    while (end < span_end_ptr(span) && (isalnum((unsigned char)*end) || *end == '_' || *end == '$')) end++;
    return span_make_in(span, start, end);
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

// The logical operand whose binary_expr_typecheck() is next (its parent is a logical operator: not the top of a tree of them), and the condition of the `if`
// being checked, whose logical operators expr_typecheck_if_condition() looks at
static thread_local Expr const* logical_operand;
static thread_local Expr const* if_condition;

// tsc's checkTestingKnownTruthyCallableOrAwaitableOrEnumMemberType: a condition that tests a function or a promise
// without calling or awaiting it in the condition or the body (TS2774, TS2801), or an enum member (TS2845). Parentheses
// aren't nodes here, so the parents tsc looks at are kept on the way down: a path of frames from the condition
typedef struct CondPath {
    Expr const* node;
    struct CondPath const* up; // null at the condition, whose parent is no binary expression that matters
} CondPath;

static bool is_logical_binary(Expr const* me) {
    return my tag == EXPR_BINARY_OP && binary_op_is_logical(my binary_op->op);
}

// The binary expression that is the node's parent (tsc's node.parent when isBinaryExpression), else null
static Expr const* cond_path_binary_parent(CondPath const* path) {
    if (expr_is_perenthisized(path->node) || !path->up) return nullptr;
    return path->up->node->tag == EXPR_BINARY_OP ? path->up->node : nullptr;
}

// What tsc's getSymbolAtLocation gives for an identifier or the name of a property access, as a key to compare: the
// symbol of an identifier, the declaration of a property. Null if there is nothing to compare
static void const* location_symbol(Expr const* me) {
    if (my tag == EXPR_IDENTIFIER) return my identifier.keyword ? nullptr : my identifier.symbol;
    if (my tag != EXPR_PROPERTY || my property->form != PROPERTY_EXPR_DOT) return nullptr;
    Checker* checker = current_checker;
    Ty const* key = ty_access_key_type(checker, my property);
    if (!(key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) || (key->flags & TYF_ENUM_LITERAL)) return nullptr;
    Atom const* name = ty_literal_name(checker, key);
    Ty const* object = ty_apparent_type(checker, ty_expr_type(checker, &my property->expr));
    Prop const* prop = nullptr;
    if (object->kind == TY_UNION) {
        for (u32 i = 0; i < object->composite.types.len && !prop; i++) {
            prop = ty_property_of_type(checker, ty_apparent_type(checker, object->composite.types.data[i]), name);
        }
    } else {
        prop = ty_property_of_type(checker, object, name);
    }
    if (!prop) return nullptr;
    while (prop->target) prop = prop->target;
    if (prop->decl->symbol) return prop->decl->symbol;
    if (prop->decl->decl_span_count) return prop->decl->decl_spans[0].buf;
    return prop->decl != &ty_prop_decl_none && prop->decl != &ty_prop_decl_value_name ? (void const*)prop->decl :
        (void const*)prop;
}

typedef struct TruthyUseSearch {
    Expr const* root; // Its children are searched, not itself (tsc's forEachChild of the node)
    void const* symbol;
    Expr const* cond; // The condition (tsc's expr in isSymbolUsedInConditionBody), null for a binary chain
    Expr const* tested; // The location that was tested
    bool tested_identifier_in_binary; // tsc's isIdentifier(testedNode) && isBinaryExpression(testedNode.parent)
} TruthyUseSearch;

// isSymbolUsedInConditionBody's walk up the tested and the found expressions in step
static bool same_access_chain(Expr const* tested, Expr const* child) {
    while (true) {
        if (tested->tag == EXPR_IDENTIFIER && child->tag == EXPR_IDENTIFIER) {
            bool tested_this = tested->identifier.keyword == KEYWORD_IDENTIFIER_THIS;
            bool child_this = child->identifier.keyword == KEYWORD_IDENTIFIER_THIS;
            if (tested_this && child_this) return true;
            if (tested_this || child_this || tested->identifier.keyword || child->identifier.keyword) return false;
            return tested->identifier.symbol == child->identifier.symbol;
        }
        if (tested->tag == EXPR_PROPERTY && child->tag == EXPR_PROPERTY &&
            tested->property->form == PROPERTY_EXPR_DOT && child->property->form == PROPERTY_EXPR_DOT
        ) {
            if (location_symbol(tested) != location_symbol(child)) return false;
            tested = &tested->property->expr;
            child = &child->property->expr;
        } else if (tested->tag == EXPR_CALL && child->tag == EXPR_CALL) {
            tested = &tested->call->callable;
            child = &child->call->callable;
        } else {
            return false;
        }
    }
}

static bool truthy_use_found(Expr const* e, TruthyUseSearch const* search) {
    bool name = e->tag == EXPR_PROPERTY && e->property->form == PROPERTY_EXPR_DOT; // the name is a child of e
    if (!name && (e == search->root || e->tag != EXPR_IDENTIFIER)) return false;
    if (location_symbol(e) != search->symbol) return false;
    if (!search->cond) return true; // isSymbolUsedInBinaryExpressionChain
    if (search->cond->tag == EXPR_IDENTIFIER || search->tested_identifier_in_binary) return true;
    if (!name) return false; // (an identifier's parent isn't the tested property access)
    return same_access_chain(search->tested, e);
}

static AstWalk truthy_use_visit(Expr const* e, void* arg) {
    return truthy_use_found(e, arg) ? AST_WALK_STOP : AST_WALK_CONTINUE;
}

typedef struct TruthyBody {
    Statement const* statement;
    Expr const* expr;
} TruthyBody;

static bool truthy_symbol_used(Expr const* root, TruthyUseSearch* search) {
    search->root = root;
    return ast_walk_expr(root, truthy_use_visit, search);
}

static void truthy_both_helper(Expr const* cond, CondPath const* path, TruthyBody body, ErrorCtx* errs);

static void truthy_helper(Expr const* cond, CondPath const* path, TruthyBody body, ErrorCtx* errs) {
    Checker* checker = current_checker;
    CondPath right_frame;
    Expr const* location = cond;
    CondPath const* location_path = path;
    if (is_logical_binary(cond)) {
        location = &cond->binary_op->right;
        right_frame = (CondPath){ location, path };
        location_path = &right_frame;
    }
    if (expr_is_module_exports(location)) return;
    if (is_logical_binary(location)) {
        truthy_both_helper(location, location_path, body, errs);
        return;
    }
    Ty const* type = ty_expr_type(checker, location);
    bool dot = location->tag == EXPR_PROPERTY && location->property->form == PROPERTY_EXPR_DOT;
    if ((type->flags & TYF_ENUM_LITERAL) && dot) {
        Expr const* object = &location->property->expr;
        // (`E.A`, `ns.E.A`: the resolved symbol of the object)
        Symbol const* enum_symbol = object->tag == EXPR_IDENTIFIER ? object->identifier.target : expr_entity_name_symbol(object);
        if (enum_symbol && (enum_symbol->flags & SYMBOL_ENUM)) {
            Value const* value = &type->literal.value;
            bool truthy = value->tag == VALUE_STRING ? type->literal.string && type->literal.string->len != 0
                : value->tag == VALUE_NUMBER ? value->number != 0 && value->number == value->number : true;
            error_ctx_error_d(errs, location->inner_span, 2845, "This condition will always return '",
                truthy ? "true" : "false", "'.");
            return;
        }
    }
    bool cast = dot && location->property->expr.tag == EXPR_AS_TYPE && !location->property->expr.as_type->is_satisfies;
    if (!ty_can_be_truthy(checker, type) || cast) return;
    bool promise = ty_is_promise(checker, type);
    if (!ty_call_signature_count(checker, type) && !promise) return;
    void const* symbol = location->tag == EXPR_IDENTIFIER || dot ? location_symbol(location) : nullptr;
    if (!symbol && !promise) return;
    bool used = false;
    if (symbol) {
        TruthyUseSearch search = { .symbol = symbol };
        // isSymbolUsedInBinaryExpressionChain(cond.parent)
        for (CondPath const* p = path; !used; p = p->up) {
            Expr const* parent = cond_path_binary_parent(p);
            if (!parent || parent->binary_op->op != BINARY_OP_AND_AND) break;
            used = truthy_symbol_used(&parent->binary_op->right, &search);
        }
        if (!used && (body.statement || body.expr)) {
            search.cond = cond;
            search.tested = location;
            search.tested_identifier_in_binary = location->tag == EXPR_IDENTIFIER &&
                (location == cond ? cond_path_binary_parent(path) != nullptr : !expr_is_perenthisized(location));
            if (body.expr) {
                used = truthy_symbol_used(body.expr, &search);
            } else {
                search.root = nullptr;
                used = ast_walk_statement(body.statement, truthy_use_visit, &search);
            }
        }
    }
    if (used) return;
    if (promise) {
        cstr name = ty_to_cstr(checker, type);
        Error* error = error_ctx_error_d(errs, location->inner_span, 2801,
            "This condition will always return true since this '", name, "' is always defined.");
        error_add_related_d(error, location->inner_span, 2773, "Did you forget to use 'await'?");
        cstr_drop(&name);
    } else {
        error_ctx_error_d(errs, location->inner_span, 2774,
            "This condition will always return true since this function is always defined. Did you mean to call it instead?");
    }
}

static void truthy_both_helper(Expr const* cond, CondPath const* path, TruthyBody body, ErrorCtx* errs) {
    truthy_helper(cond, path, body, errs);
    if (cond->tag == EXPR_BINARY_OP && (cond->binary_op->op == BINARY_OP_OR_OR || cond->binary_op->op == BINARY_OP_EH_EH)) {
        CondPath frame = { &cond->binary_op->left, path };
        truthy_both_helper(&cond->binary_op->left, &frame, body, errs);
    }
}

static void check_known_truthy(Expr const* cond, CondPath const* path, TruthyBody body, ErrorCtx* errs) {
    if (!compiler_options()->strict_null_checks) return;
    truthy_both_helper(cond, path, body, errs);
}

// The logical expressions under `root` through parentheses and other logical expressions (their parent as far as tsc's
// checkBinaryLikeExpression is concerned is root's parent): each checks its left operand when it is `&&` or when
// root is the condition of an `if`, with the `if`'s body
static void check_known_truthy_tree(Expr const* me, CondPath const* path, bool in_if, TruthyBody body, ErrorCtx* errs) {
    if (!is_logical_binary(me)) return;
    CondPath left = { &my binary_op->left, path };
    CondPath right = { &my binary_op->right, path };
    check_known_truthy_tree(&my binary_op->left, &left, in_if, body, errs);
    if (my binary_op->op == BINARY_OP_AND_AND || in_if) check_known_truthy(&my binary_op->left, &left, body, errs);
    check_known_truthy_tree(&my binary_op->right, &right, in_if, body, errs);
}

// The condition of an `if` statement
void expr_typecheck_if_condition(Expr const* me, ErrorCtx* errs) {
    Expr const* outer = if_condition;
    if_condition = me;
    expr_typecheck(me, errs);
    if_condition = outer;
    expr_check_truthiness(me, errs);
}

// The tested functions and promises of an `if` statement's condition that its body `then` never uses (TS2774,
// TS2801). After the body is checked: the search reads the types of the body's property accesses, and an expression
// is typed by whoever knows its contextual type first (a parameter of a function expression typed early is any)
void expr_check_if_known_truthy(Expr const* me, Statement const* then, ErrorCtx* errs) {
    if (!compiler_options()->strict_null_checks) return;
    CondPath root = { me, nullptr };
    TruthyBody body = { .statement = then };
    check_known_truthy(me, &root, body, errs);
    check_known_truthy_tree(me, &root, true, body, errs);
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
                case BINARY_OP_OR_OR: case BINARY_OP_OR_OR_EQUAL: case BINARY_OP_AND_AND: case BINARY_OP_AND_AND_EQUAL:
                    return PREDICATE_SOMETIMES;
                // (The right operand decides)
                case BINARY_OP_COMMA: case BINARY_OP_EQUAL:
                    return nullish_semantics(&my binary_op->right);
                // (tsgo: the left operand's non-null path, and the right operand's if the left may be nullish)
                case BINARY_OP_EH_EH: case BINARY_OP_EH_EH_EQUAL: {
                    PredicateSemantics left = nullish_semantics(&my binary_op->left);
                    PredicateSemantics result = left & PREDICATE_NEVER;
                    if (left & PREDICATE_ALWAYS) result |= nullish_semantics(&my binary_op->right);
                    return result;
                }
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
// tsc's checkGrammarImportCallExpression
static void import_call_check_grammar(Expr const* me, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    ModuleKind module = options->module;
    if (options->verbatim_module_syntax && module == MODULE_KIND_COMMONJS) {
        source_file_verbatim_cjs_error(span_source(my inner_span), errs, my inner_span, true);
        return;
    }
    Expr const* callee = my call->callable.tag == EXPR_GENERIC_PARAMS ? &my call->callable.generic_params->expr : &my call->callable;
    if (callee->tag == EXPR_IDENTIFIER && callee->identifier.import_source) {
        if (module != MODULE_KIND_ESNEXT && module != MODULE_KIND_NODENEXT && module != MODULE_KIND_PRESERVE) {
            error_ctx_grammar_error_d(errs, my inner_span, 18113, "Source phase imports are only supported when the "
                "'--module' option is set to 'esnext', 'nodenext', or 'preserve'.");
            return;
        }
    } else if (callee->tag == EXPR_IDENTIFIER && callee->identifier.import_defer) {
        if (module != MODULE_KIND_ESNEXT && module != MODULE_KIND_PRESERVE) {
            error_ctx_grammar_error_d(errs, my inner_span, 18060,
                "Deferred imports are only supported when the '--module' flag is set to 'esnext' or 'preserve'.");
            return;
        }
    } else if (module == MODULE_KIND_ES2015) {
        error_ctx_grammar_error_d(errs, my inner_span, 1323,
            "Dynamic imports are only supported when the '--module' flag is set to 'es2020', 'es2022', 'esnext', "
            "'commonjs', 'amd', 'system', 'umd', 'node16', 'node18', 'node20', or 'nodenext'.");
        return;
    }
    if (callee->tag == EXPR_IDENTIFIER && callee->identifier.import_source && my call->optional_chain) {
        Span question_dot = span_token_after(callee->inner_span, "?.");
        error_ctx_grammar_error_d(errs, question_dot, 18114, "Optional chaining cannot be used with 'import.source'.");
        return;
    }
    if (my call->callable.tag == EXPR_GENERIC_PARAMS) {
        error_ctx_grammar_error_d(errs, my inner_span, 1326, "This use of 'import' is invalid. 'import()' calls can be "
            "written, but they must have parentheses and cannot have type arguments.");
        return;
    }
    VecExpr const* args = &my call->args;
    isize count = vec_expr_size(args);
    bool node = module >= MODULE_KIND_NODE16 && module <= MODULE_KIND_NODENEXT;
    if (!node && module != MODULE_KIND_ESNEXT && module != MODULE_KIND_PRESERVE && count > 0) {
        // (checkGrammarForDisallowedTrailingComma: the AST doesn't keep it, the source does)
        Span comma = span_comma_after(expr_outer_span(vec_expr_at(args, count - 1)));
        if (span_valid(comma)) error_ctx_grammar_error_d(errs, comma, 1009, "Trailing comma not allowed.");
    }
    if (!node && module != MODULE_KIND_ESNEXT && module != MODULE_KIND_PRESERVE && count > 1) {
        error_ctx_grammar_error_d(errs, expr_outer_span(vec_expr_at(args, 1)), 1324,
            "Dynamic imports only support a second argument when the '--module' option is set to 'esnext', 'node16', "
            "'node18', 'node20', 'nodenext', or 'preserve'.");
        return;
    }
    if (count == 0 || count > 2) {
        error_ctx_grammar_error_d(errs, my inner_span, 1450,
            "Dynamic imports can only accept a module specifier and an optional set of attributes as arguments");
        return;
    }
    for_each (i, vec_expr, args) {
        if (i->tag != EXPR_SPREAD) continue;
        error_ctx_grammar_error_d(errs, expr_outer_span(i), 1325, "Argument of dynamic import cannot be spread element.");
        return;
    }
}

static bool is_unparenthesized_binary(Expr const* me) {
    return my tag == EXPR_BINARY_OP && !expr_is_perenthisized(me);
}

static void mixed_without_parentheses(Expr const* operand, char const* first, char const* second, ErrorCtx* errs) {
    Span at = expr_outer_span(operand);
    if (!span_source(at) || span_source(at)->has_parse_errors) return; // (A grammar error)
    error_ctx_error_d(errs, at, 5076, "'", first, "' and '", second, "' operations cannot be mixed without parentheses.");
}

// tsc's checkNullishCoalesceOperands for a `??` operand of `||` (whose left operand is a binary expression), from the
// `||`: `a || b ?? c` and `a ?? b || c` (where the `??` is the left operand) say so about the left operand
static void or_or_check_nullish_operand(Expr const* me, ErrorCtx* errs) {
    Expr const* left = &my binary_op->left;
    if (!is_unparenthesized_binary(left)) return;
    if (left->binary_op->op == BINARY_OP_EH_EH || (is_unparenthesized_binary(&my binary_op->right) && my binary_op->right.binary_op->op == BINARY_OP_EH_EH)) {
        mixed_without_parentheses(left, "?" "?", "||", errs);
    }
}

static void nullish_coalesce_typecheck(Expr const* me, ErrorCtx* errs) {
    // (Only when the `??` isn't itself an operand: then its parent's check above is the one)
    if (!my binary_op->is_operand) {
        Expr const* left = &my binary_op->left;
        Expr const* right = &my binary_op->right;
        if (is_unparenthesized_binary(left)) {
            BinaryOp op = left->binary_op->op;
            if (op == BINARY_OP_OR_OR || op == BINARY_OP_AND_AND) mixed_without_parentheses(left, binary_op_get_zv(op).str, "?" "?", errs);
        } else if (is_unparenthesized_binary(right) && right->binary_op->op == BINARY_OP_AND_AND) {
            mixed_without_parentheses(right, "?" "?", "&&", errs);
        }
    }
    Expr const* left = skip_outer_expressions(&my binary_op->left);
    PredicateSemantics semantics = nullish_semantics(left);
    if (semantics == PREDICATE_SOMETIMES) return;
    if (semantics == PREDICATE_ALWAYS) {
        error_ctx_error_d(errs, node_error_span(left), 2871, "This expression is always nullish.");
    } else {
        error_ctx_error_d(errs, node_error_span(left), 2869,
            "Right operand of ?? is unreachable because the left operand is never nullish.");
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

static void object_check_grammar(Expr const* me, ErrorCtx* errs) {
    if (!span_source(expr_outer_span(me)) || span_source(expr_outer_span(me))->has_parse_errors || my assignment_pattern) return;
    enum { GET = 1, SET = 2, PROPERTY = 4, METHOD = 8, ACCESSOR = GET | SET };
    ObjectExpr const* object = &my object;
    isize count = vec_object_expr_item_size(object);
    uint8_t* seen = calloc(count ? (size_t)count : 1, 1);
    Atom const** names = calloc(count ? (size_t)count : 1, sizeof(Atom const*));
    panic_if(!seen || !names, "out of memory");
    NameIndex index = name_index_make(count);
    for (isize n = 0; n < count; n++) {
        ObjectExprItem const* item = vec_object_expr_item_at(object, n);
        uint8_t kind;
        switch (item->tag) {
            case OBJECT_ITEM_PROPERTY: case OBJECT_ITEM_SHORTHAND:
                kind = PROPERTY;
                if (item->question_offset) {
                    char const* question = span_end_ptr(item->name_span) + item->question_offset - 1;
                    error_ctx_grammar_error_d(errs, span_make_in(item->name_span, question, question + 1), 1162,
                        "An object member cannot be declared optional.");
                }
                break;
            case OBJECT_ITEM_METHOD: kind = METHOD; break;
            case OBJECT_ITEM_GETTER: kind = GET; break;
            case OBJECT_ITEM_SETTER: kind = SET; break;
            default: continue;
        }
        names[n] = object_item_effective_name(item);
        if (!names[n]) continue;
        isize existing = -1;
        u64 slot = name_index_slot(&index, (u64)(uintptr_t)names[n]);
        for (; index.slots[slot] >= 0; slot = name_index_next(&index, slot)) {
            if (names[index.slots[slot]] == names[n]) {
                existing = index.slots[slot];
                break;
            }
        }
        if (existing < 0) {
            seen[n] = kind;
            index.slots[slot] = n;
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
    name_index_drop(&index);
}

// tsc's binder declares the members of an object literal like any symbols (TypeScript 7's excludes): a property
// excludes methods, a method everything, an accessor everything but its other half and properties. A conflict is TS2300
// on every declaration of the table's symbol of the name and on the new one, which is a symbol of its own that the
// table doesn't keep (an accessor conflicting with what isn't its other half makes the table's symbol both accessors:
// tsgo's declareSymbolEx). Not a grammar check: also with parse errors
static void object_bind_members(Expr const* me, ErrorCtx* errs) {
    enum { PROPERTY = 1, METHOD = 2, GET = 4, SET = 8, VALUE = PROPERTY | METHOD | GET | SET };
    ObjectExpr const* object = &my object;
    isize count = vec_object_expr_item_size(object);
    // Per item: the item whose symbol it was declared into (-1: none), and for the table's symbols (in `index`) their
    // flags
    uint8_t* flags = calloc(count ? (size_t)count : 1, 1);
    isize* start = malloc(sizeof(isize) * (count ? (size_t)count : 1));
    panic_if(!flags || !start, "out of memory");
    NameIndex index = name_index_make(count);
    for (isize n = 0; n < count; n++) {
        ObjectExprItem const* item = vec_object_expr_item_at(object, n);
        uint8_t includes, excludes;
        switch (item->tag) {
            case OBJECT_ITEM_PROPERTY: case OBJECT_ITEM_SHORTHAND: includes = PROPERTY; excludes = METHOD; break;
            case OBJECT_ITEM_METHOD: includes = METHOD; excludes = VALUE; break;
            case OBJECT_ITEM_GETTER: includes = GET; excludes = VALUE & ~(SET | PROPERTY); break;
            case OBJECT_ITEM_SETTER: includes = SET; excludes = VALUE & ~(GET | PROPERTY); break;
            default: start[n] = -1; continue;
        }
        start[n] = n;
        if (item->computed_key || !span_valid(item->name_span)) {
            start[n] = -1;
            continue;
        }
        // The table's symbol of that name
        isize table = -1;
        csview name = cstr_sv(&item->name_string);
        u64 slot = name_index_slot(&index, symbol_name_hash(name));
        for (; index.slots[slot] >= 0; slot = name_index_next(&index, slot)) {
            if (cstr_equals_sv(&vec_object_expr_item_at(object, index.slots[slot])->name_string, name)) {
                table = index.slots[slot];
                break;
            }
        }
        if (table < 0) {
            flags[n] = includes;
            index.slots[slot] = n;
            continue;
        }
        if (!(flags[table] & excludes)) {
            flags[table] |= includes;
            start[n] = table;
            continue;
        }
        if ((flags[table] & (GET | SET)) && (flags[table] & (GET | SET)) != (includes & (GET | SET))) flags[table] |= GET | SET;
        for (isize j = table; j < n; j++) {
            ObjectExprItem const* other = vec_object_expr_item_at(object, j);
            if (start[j] != table) continue;
            Span at = other->name_span;
            if (!error_ctx_find(errs, at, 2300)) error_ctx_error_d(errs, at, 2300, "Duplicate identifier '", at, "'.");
        }
        if (!error_ctx_find(errs, item->name_span, 2300)) error_ctx_error_d(errs, item->name_span, 2300, "Duplicate identifier '", item->name_span, "'.");
        flags[n] = includes;
    }
    free(flags);
    free(start);
    name_index_drop(&index);
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
        case EXPR_IMPLICIT_ANY: // A missing operand, tsc's missing identifier
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
        error_ctx_error_d(errs, expr_error_span(operand), 2356,
            "An arithmetic operand must be of type 'any', 'number', 'bigint' or an enum type.");
        return;
    }
    Expr const* reference = operand;
    while (reference->tag == EXPR_AS_TYPE || (reference->tag == EXPR_UNARY_OP && reference->unary_op->op == UNARY_OP_RIGHT_BANG)) {
        reference = reference->tag == EXPR_AS_TYPE ? &reference->as_type->expr : &reference->unary_op->expr;
    }
    if (((reference->tag != EXPR_IDENTIFIER || reference->identifier.keyword) && reference->tag != EXPR_PROPERTY) ||
        ty_is_import_meta(reference)
    ) {
        error_ctx_error_d(errs, expr_error_span(operand), 2357,
            "The operand of an increment or decrement operator must be a variable or a property access.");
    } else if (expr_is_optional_chain(reference)) {
        error_ctx_error_d(errs, expr_error_span(operand), 2777,
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
        Span keyword = expr_outer_span(me); // (what tsc reports it at)
        if (keyword.size > 5) keyword.size = 5;
        if (async && target < SCRIPT_TARGET_ES2018) {
            ty_check_emit_helpers(checker, keyword,
                EMIT_HELPER_AWAIT | EMIT_HELPER_ASYNC_DELEGATOR | EMIT_HELPER_ASYNC_VALUES, errs);
        }
    }
    Ty const* yielded;
    Span span = yield->expr.tag == EXPR_IMPLICIT_ANY ? expr_outer_span(me) : expr_outer_span(&yield->expr);
    if (yield->op == UNARY_OP_YIELD_STAR) {
        u32 use = async ? ITERATION_ASYNC_YIELD_STAR : ITERATION_YIELD_STAR;
        // (What the generator is sent, by its annotation: tsc's checkYieldExpression, signatureNextType)
        Ty const* sent = nullptr;
        if (generator->ret_explicit) {
            TyIterationTypes const* own = ty_generator_return_iteration_types(checker,
                ty_from_node(checker, &generator->ret, (LowerCtx){ .generic = true }), async);
            sent = own ? own->next_type : nullptr;
        }
        yielded = ty_iterated_type_sending(checker, use, ty_expr_type(checker, &yield->expr), sent, span, errs);
    } else {
        yielded = yield->expr.tag == EXPR_IMPLICIT_ANY ? ty_intrinsic(checker, TY_UNDEFINED) : ty_expr_type(checker, &yield->expr);
    }
    if (async) yielded = ty_awaited(checker, yielded);
    // tsc's checkYieldExpression: what the generator is sent is any without an annotation or contextual type
    if (!generator->ret_explicit && yield->op == UNARY_OP_YIELD && compiler_options()->no_implicit_any && !yield->result_unused) {
        TyIterationTypes const* context = ty_generator_context(checker, generator);
        LazyMemo const* memo = lazy_type_state_peek((LazyType*)&me->lazy_type);
        Ty const* contextual = memo && memo->done ? lazy_memo_context(memo) : nullptr;
        // (A type parameter that isn't in scope is the one of a call's signature the argument was checked against
        // while inferring: the resolved signature has it inferred from this yield, which is any)
        bool call_type_param = contextual && contextual->kind == TY_TYPE_PARAM && yield->no_type_params_in_scope;
        if (!context && (!contextual || (contextual->flags & TYF_ANY) || call_type_param)) {
            Span keyword = expr_outer_span(me); // (the `yield`)
            if (keyword.size > 5) keyword.size = 5;
            error_ctx_error_d(errs, keyword, 7057, "'yield' expression implicitly results in an 'any' type because "
                "its containing generator lacks a return-type annotation.");
        }
    }
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
    if (ty_maybe_type_of_kind(ty, kind)) return true;
    if (!constraint) return false;
    // (tsc's maybeTypeOfKindConsideringBaseConstraint: the base constraint of the whole type, `keyof T & string` is a
    // string)
    Ty const* base = ty_base_constraint(current_checker, ty);
    return base && base != ty && ty_maybe_type_of_kind(base, kind);
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

// A shift by a constant of 32 or more (tsc's checkBinaryLikeExpressionWorker): the same as by the remainder. A suggestion,
// an error in an enum member's initializer (whose value is computed)
static void check_overshift(Checker* checker, Expr const* expr, ErrorCtx* errs) {
    bool in_enum = checker->enum_member_initializer == expr;
    if (!in_enum && !compiler_options()->capture_suggestions) return;
    double amount;
    if (!ty_evaluate_number(checker, &expr->binary_op->right, &amount) || !(fabs(amount) >= 32)) return;
    Value remainder = { .tag = VALUE_NUMBER, .number = fmod(amount, 32) };
    if (remainder.number == 0) remainder.number = 0; // (JavaScript prints -0 as 0)
    cstr amount_str = cstr_null;
    cstr_append_value_js(&amount_str, &remainder);
    Error* e = error_ctx_error_d(errs, expr->inner_span, 6807, "This operation can be simplified. This shift is identical to `",
        expr_outer_span(&expr->binary_op->left), " ", binary_op_get_zv(expr->binary_op->op).str, " ", amount_str, "`.");
    if (e && !in_enum) e->suggestion = true;
    cstr_drop(&amount_str);
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
    // (Operands that would fit once awaited: "Did you forget to use 'await'?", and shown as they are)
    Ty const* awaited_left = ty_awaited_no_alias(checker, left);
    Ty const* awaited_right = ty_awaited_no_alias(checker, right);
    bool with_await = (awaited_left != left || awaited_right != right) &&
        assignable_to_kind(awaited_left, kind, false) && assignable_to_kind(awaited_right, kind, false);
    Ty const* left_base = ty_base_type_of_literal(checker, left);
    Ty const* right_base = ty_base_type_of_literal(checker, right);
    bool related = assignable_to_kind(left_base, kind, false) && assignable_to_kind(right_base, kind, false);
    if (!related && !with_await) {
        left = left_base;
        right = right_base;
    }
    BinaryOp written = expr->binary_op->op;
    // (Shown like tsc's getTypeNamesForErrorDisplay: an object literal or function expression from where it's written)
    cstr left_str, right_str;
    ty_display_pair_cstr(checker, left, right, &left_str, &right_str);
    Error* e = error_ctx_error_d(errs, expr->inner_span, 2365, "Operator '", binary_op_get_zv(written).str,
        "' cannot be applied to types '", left_str, "' and '", right_str, "'.");
    c_drop(cstr, &left_str, &right_str);
    if (with_await) error_add_related_d(e, expr->inner_span, 2773, "Did you forget to use 'await'?");
}

// The right side of `instanceof`: callable, constructable or with a [Symbol.hasInstance] (of every union member, some
// intersection member)
static bool instanceof_target_ok(Checker* checker, Ty const* ty) {
    // A union has the signatures all of its members have (tsc's getUnionSignatures: `AA | BB` of a call and a construct
    // signature has neither), or a `[Symbol.hasInstance]` if they all do
    if (ty->kind == TY_UNION) {
        u32 count = 0;
        ty_union_signatures(checker, ty->composite.types, false, &count);
        if (count) return true;
        ty_union_signatures(checker, ty->composite.types, true, &count);
        if (count) return true;
        // (Of a member with signatures also Function's, tsc's getPropertyOfType: `[Symbol.hasInstance]` from the lib)
        Atom const* has_instance = ty_well_known_symbol_name(checker, "hasInstance");
        if (!has_instance) return false;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* apparent = ty_apparent_type(checker, ty->composite.types.data[i]);
            if (!(apparent->flags & TYF_OBJECT)) return false;
            if (!ty_has_prop(checker, apparent, has_instance)) return false;
        }
        return true;
    }
    if (ty->kind == TY_INTERSECTION) {
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
        csview name = atom_sv(members_prop_name(members, i));
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
    if (!span_eq(my inner_span, expr_outer_span(me))) return false;
    switch (my tag) {
        case EXPR_OBJECT: case EXPR_ARRAY: case EXPR_REGEX: case EXPR_CLASS_DECL: return true;
        case EXPR_FUNCTION: return !function_impl_is_arrow(me->function);
        default: return false;
    }
}


// Returns whether a compound assignment is checked too: not after its operands were rejected (tsc's
// checkBinaryLikeExpression, whose checkAssignmentOperator is skipped then)
// The global NaN (tsc's isGlobalNaN)
static bool is_global_nan(Expr const* me) {
    if (my tag != EXPR_IDENTIFIER || my identifier.keyword || !csview_equals(span_sv(my inner_span), "NaN")) return false;
    Symbol const* nan = ty_global_value_symbol(current_checker, c_sv("NaN"));
    return nan && my identifier.target == nan;
}

// Nothing is equal to NaN (tsc's checkNaNEquality)

static void nan_equality_typecheck(Expr const* me, ErrorCtx* errs) {
    Expr const* left = &my binary_op->left;
    Expr const* right = &my binary_op->right;
    bool left_nan = is_global_nan(left), right_nan = is_global_nan(right);
    if (!left_nan && !right_nan) return;
    BinaryOp op = my binary_op->op;
    bool eq = op == BINARY_OP_DOUBLE_EQUAL || op == BINARY_OP_TRIPLE_EQUAL;
    Error* error = error_ctx_error_d(errs, expr_outer_span(me), 2845, "This condition will always return '", eq ? "false" : "true", "'.");
    if (left_nan && right_nan) return;
    Expr const* other = left_nan ? right : left;
    csview name = expr_is_entity_name(other) ? span_sv(other->inner_span) : c_sv("...");
    error_add_related_d(error, expr_outer_span(other), 1369, "Did you mean '", eq ? "" : "!", "Number.isNaN(", name, ")'?");
}

// (Below, with the other CommonJS checks)
static bool module_exports_declared_type(Expr const* me);

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
                if (!check_symbol_operands(left, right, expr, errs)) operands_ok = false; // (tsc returns before checkAssignmentOperator)
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
                // (At the operator token)
                char const* at = span_end_ptr(expr_outer_span(&expr->binary_op->left));
                char const* end = span_start_ptr(expr_outer_span(&expr->binary_op->right));
                char op_char = binary_op_get_zv(op).str[0];
                while (at < end && *at != op_char) {
                    if (at + 1 < end && at[0] == '/' && at[1] == '*') {
                        at += 2;
                        while (at + 1 < end && !(at[0] == '*' && at[1] == '/')) at++;
                        at += 2;
                    } else if (at + 1 < end && at[0] == '/' && at[1] == '/') {
                        while (at < end && *at != '\n') at++;
                    } else {
                        at++;
                    }
                }
                Span op_span = at < end ? span_make_in(expr->inner_span, at, at + binary_op_get_zv(expr->binary_op->op).size) : expr->inner_span;
                error_ctx_error_d(errs, op_span, 2447, "The '", binary_op_get_zv(expr->binary_op->op).str,
                    "' operator is not allowed for boolean types. Consider using '", binary_op_get_zv(suggested).str, "' instead.");
                operands_ok = false;
                break;
            }
            Ty const* numeric_types[] = { ty_intrinsic(checker, TY_NUMBER), ty_intrinsic(checker, TY_BIGINT) };
            Ty const* number_or_bigint = ty_union(checker, numeric_types, 2, nullptr, (TyList){});
            // (tsc's checkArithmeticOperandType with isAwaitValid: an operand that would fit once awaited)
            if (!ty_is_related(checker, left, number_or_bigint, TY_RELATION_ASSIGNABLE)) {
                Error* e = error_ctx_error_d(errs, expr_error_span(&expr->binary_op->left), 2362,
                    "The left-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type.");
                Ty const* awaited = e ? ty_awaited_of_promise(checker, left) : nullptr;
                if (awaited && ty_is_related(checker, awaited, number_or_bigint, TY_RELATION_ASSIGNABLE)) {
                    error_add_related_d(e, expr_outer_span(&expr->binary_op->left), 2773, "Did you forget to use 'await'?");
                }
                operands_ok = false;
            }
            if (!ty_is_related(checker, right, number_or_bigint, TY_RELATION_ASSIGNABLE)) {
                Error* e = error_ctx_error_d(errs, expr_error_span(&expr->binary_op->right), 2363,
                    "The right-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type.");
                Ty const* awaited = e ? ty_awaited_of_promise(checker, right) : nullptr;
                if (awaited && ty_is_related(checker, awaited, number_or_bigint, TY_RELATION_ASSIGNABLE)) {
                    error_add_related_d(e, expr_outer_span(&expr->binary_op->right), 2773, "Did you forget to use 'await'?");
                }
                operands_ok = false;
            }
            if (operands_ok && (op == BINARY_OP_LESS_LESS || op == BINARY_OP_GREATER_GREATER ||
                op == BINARY_OP_GREATER_GREATER_GREATER)
            ) {
                check_overshift(checker, expr, errs);
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
            // (tsc's reportOperatorError: whether it would work with `await`, else getBaseTypesIfUnrelated)
            Ty const* awaited_left = ty_awaited_no_alias(checker, left);
            Ty const* awaited_right = ty_awaited_no_alias(checker, right);
            bool with_await = (awaited_left != left || awaited_right != right) &&
                comparison_operands_fit(awaited_left, awaited_right);
            Ty const* left_base = ty_base_type_of_literal(checker, left);
            Ty const* right_base = ty_base_type_of_literal(checker, right);
            if (!with_await && !comparison_operands_fit(left_base, right_base)) {
                left = left_base;
                right = right_base;
            }
            cstr left_str, right_str;
            ty_display_pair_cstr(checker, left, right, &left_str, &right_str);
            Error* e = error_ctx_error_d(errs, expr->inner_span, 2365, "Operator '", binary_op_get_zv(op).str,
                "' cannot be applied to types '", left_str, "' and '", right_str, "'.");
            c_drop(cstr, &left_str, &right_str);
            if (with_await) error_add_related_d(e, expr->inner_span, 2773, "Did you forget to use 'await'?");
        }   break;

        case BINARY_OP_EQUAL:
            if (is_container_declaration(expr->binary_op)) break;
            // `exports.x = undefined` of an export assigned more than once isn't checked (tsgo's checkAssignmentOperator)
            if (right && (right->flags & TYF_UNDEFINED) && right->kind != TY_UNION) {
                Span name = span_null;
                if (binary_commonjs_declaration(expr->binary_op, &name) == COMMONJS_TARGET_EXPORTS_PROPERTY) {
                    SourceFile const* file = span_source(expr_outer_span(expr));
                    Symbol const* module = file && file->scope ? file->scope->container : nullptr;
                    Symbol const* exported = module && module->exports ? symbol_table_get(module->exports, span_sv(name)) : nullptr;
                    if (exported && exported->decl_count > 1) break;
                }
            }
            // Nor is `module.exports = v` of an object type, which the module's other exports add to. Unless a JSDoc
            // `@type` declares it: then that's the type of `module.exports` (tsc's getWidenedTypeForAssignmentDeclaration
            // takes the first typed declaration), which every `module.exports = v` is checked against
            if (right && (right->flags & TYF_OBJECT) &&
                binary_commonjs_declaration(expr->binary_op, nullptr) == COMMONJS_TARGET_MODULE_EXPORTS &&
                !module_exports_declared_type(expr)
            ) {
                break;
            }
            {
                // (tsc's checkBinaryLikeExpression: the assignment is the current node, where a relation without an
                // error node reports, TS2859)
                CurrentNode saved = ty_enter_expr(current_checker, expr);
                assignment_target_typecheck(&expr->binary_op->left, right, &expr->binary_op->right,
                    expr->binary_op->access_site, errs);
                ty_leave_node(current_checker, saved);
            }
            break;

        case BINARY_OP_DOUBLE_EQUAL:
        case BINARY_OP_TRIPLE_EQUAL:
        case BINARY_OP_NOT_EQUAL:
        case BINARY_OP_TRIPLE_NOT_EQUAL: {
            // A fresh object is equal to nothing else (only `===` and `!==` in JS)
            bool strict_op = op == BINARY_OP_TRIPLE_EQUAL || op == BINARY_OP_TRIPLE_NOT_EQUAL;
            Expr const* l = &expr->binary_op->left;
            if ((is_literal_expression_of_object(l) || is_literal_expression_of_object(&expr->binary_op->right)) &&
                (!span_source(l->inner_span) || !span_source(l->inner_span)->is_js || strict_op)
            ) {
                bool eq = op == BINARY_OP_DOUBLE_EQUAL || op == BINARY_OP_TRIPLE_EQUAL;
                error_ctx_error_d(errs, expr_outer_span(expr), 2839, "This condition will always return '", eq ? "false" : "true",
                    "' since JavaScript compares objects by reference, not value.");
            }
            nan_equality_typecheck(expr, errs);
        }
            // (Anything may be compared with null and undefined: tsc's isTypeEqualityComparableTo())
            if (!((left->flags | right->flags) & TYF_NULLABLE) && !ty_is_comparable_either_way(current_checker, left, right)) {
                // Would they overlap once awaited? Else they're shown as their base types if those don't overlap either
                // (`n == b` for a b narrowed to `false`)
                Ty const* awaited_left = ty_awaited_no_alias(current_checker, left);
                Ty const* awaited_right = ty_awaited_no_alias(current_checker, right);
                bool with_await = (awaited_left != left || awaited_right != right) &&
                    (((awaited_left->flags | awaited_right->flags) & TYF_NULLABLE) ||
                        ty_is_comparable_either_way(current_checker, awaited_left, awaited_right));
                Ty const* left_base = ty_base_type_of_literal(current_checker, left);
                Ty const* right_base = ty_base_type_of_literal(current_checker, right);
                if (!with_await && !ty_is_comparable_either_way(current_checker, left_base, right_base)) {
                    left = left_base;
                    right = right_base;
                }
                cstr left_str, right_str;
                ty_display_pair_cstr(current_checker, left, right, &left_str, &right_str);
                // (At the binary expression, inside its parentheses)
                Error* e = error_ctx_error_d(errs, expr->inner_span, 2367,
                    "This comparison appears to be unintentional because the types '", left_str,
                    "' and '", right_str, "' have no overlap.");
                c_drop(cstr, &left_str, &right_str);
                if (with_await) error_add_related_d(e, expr->inner_span, 2773, "Did you forget to use 'await'?");
            }
            break;

        case BINARY_OP_INSTANCEOF: {
            // tsc's checkExpressionWithTypeArguments: `x instanceof C<T>`, parenthesized or not
            if (expr->binary_op->right.tag == EXPR_GENERIC_PARAMS) {
                error_ctx_error_d(errs, expr->binary_op->right.inner_span, 2848,
                    "The right-hand side of an 'instanceof' expression must not be an instantiation expression.");
            }
            // tsc's checkInstanceOfExpression: the left side can't be a primitive
            Checker* checker = current_checker;
            u32 primitive = TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT | TYF_BIGINT_LITERAL | TYF_BOOLEAN |
                TYF_BOOLEAN_LITERAL | TYF_ENUM | TYF_ES_SYMBOL_LIKE | TYF_VOID | TYF_NULLABLE;
            TyList parts = left->kind == TY_UNION ? left->composite.types : (TyList){ .data = &left, .len = 1 };
            bool all_primitive = !(left->flags & TYF_ANY); // (never too: tsc's isTypeAssignableToKind, never fits every kind)
            for (u32 i = 0; i < parts.len && all_primitive; i++) all_primitive = assignable_to_kind(parts.data[i], primitive, false);
            if (all_primitive) {
                error_ctx_error_d(errs, expr_error_span(&expr->binary_op->left), 2358, "The left-hand side of an 'instanceof' "
                    "expression must be of type 'any', an object type or a type parameter.");
            }
            // (tsc's resolveInstanceofExpression) the right side something callable or constructable
            if (right->flags & TYF_ANY) break;
            ty_check_instanceof_has_instance(checker, left, right, expr_error_span(&expr->binary_op->left),
                expr_error_span(&expr->binary_op->right), errs);
            if (instanceof_target_ok(checker, right)) break;
            Ty const* function_type = ty_global_function_type(checker);
            if (!function_type || ty_is_related(checker, right, function_type, TY_RELATION_SUBTYPE)) break;
            error_ctx_error_d(errs, expr_error_span(&expr->binary_op->right), 2359, "The right-hand side of an 'instanceof' "
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
            // A compound assignment doesn't destructure: an array or object literal is no reference (TS2364)
            Expr const* target = skip_outer_expressions(&expr->binary_op->left);
            if (target->tag == EXPR_ARRAY || target->tag == EXPR_OBJECT) {
                if (binary_op_typecheck(left, without_assignment, right, expr, errs)) {
                    error_ctx_error_d(errs, expr_outer_span(&expr->binary_op->left), 2364,
                        "The left-hand side of an assignment expression must be a variable or a property access.");
                }
                break;
            }
            // The expression's own type is what gets assigned, the right operand's for `&&=`, `||=` and `??=` (tsc's
            // checkBinaryLikeExpressionWorker: checkAssignmentOperator(rightType))
            bool logical = op == BINARY_OP_AND_AND_EQUAL || op == BINARY_OP_OR_OR_EQUAL || op == BINARY_OP_EH_EH_EQUAL;
            if (binary_op_typecheck(left, without_assignment, right, expr, errs)) {
                binary_op_typecheck(left, BINARY_OP_EQUAL, logical ? right : expr_get_type((Expr*)expr), expr, errs);
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
        (my tag == EXPR_PROPERTY && my property->assignment_target) ||
        ((my tag == EXPR_OBJECT || my tag == EXPR_ARRAY) && my assignment_pattern); // (`[...{ 0: a }] = e`)
}

// Private names are emitted with WeakMaps and the __classPrivateField* helpers
static bool private_names_lowered(void) {
    return compiler_options()->target < SCRIPT_TARGET_ESNEXT || !compiler_options()->use_define_for_class_fields;
}

static void private_access_check_emit_helpers(Expr const* me, ErrorCtx* errs) {
    if (!private_names_lowered()) return;
    uint8_t target = my property->assignment_target;
    if (target != FLOW_TARGET_NONE) ty_check_emit_helpers(current_checker, expr_outer_span(me), EMIT_HELPER_CLASS_PRIVATE_FIELD_SET, errs);
    if (target != FLOW_TARGET_DEFINITE) ty_check_emit_helpers(current_checker, expr_outer_span(me), EMIT_HELPER_CLASS_PRIVATE_FIELD_GET, errs);
}

// An element or fragment: its tag, attributes and children (for an element, Checker.defer_checks defers this)
void jsx_element_typecheck(Expr const* me, ErrorCtx* errs) {
    // tsc's checkJsxPreconditions
    if (compiler_options()->jsx == JSX_NONE) {
        error_ctx_error_d(errs, my jsx->opening_span, 17004, "Cannot use JSX unless the '--jsx' flag is provided.");
    }
    bool intrinsic = my tag == EXPR_JSX_FRAGMENT || jsx_element_is_intrinsic(my jsx) ||
        !my jsx->tag_name.inner_span.size;
    if (!intrinsic) expr_typecheck(&my jsx->tag_name, errs);
    // The closing tag's name is checked too (tsc's checkJsxElementDeferred: checkExpression of a non-intrinsic closing
    // tag name): `</React.StrictMode>` is a TS2339 of its own. (An identifier's TS2304 is the name lookup's)
    if (my tag == EXPR_JSX_ELEMENT && my jsx->closing_name.tag == EXPR_PROPERTY && !jsx_closing_is_intrinsic(my jsx)) {
        bool saved = current_checker->in_jsx_closing_name;
        current_checker->in_jsx_closing_name = true;
        expr_typecheck(&my jsx->closing_name, errs);
        current_checker->in_jsx_closing_name = saved;
    }
    for_each (i, vec_spanned_type, &my jsx->type_args) type_typecheck(&i->type, errs);
    if (span_valid(my jsx->type_args_trailing_comma)) {
        error_ctx_grammar_error_d(errs, my jsx->type_args_trailing_comma, 1009, "Trailing comma not allowed.");
    }
    // tsc's checkGrammarJsxName: a namespaced name whose namespace isn't intrinsic-like, when JSX is transformed
    JsxKind jsx_kind = compiler_options()->jsx;
    if (my tag == EXPR_JSX_ELEMENT && my jsx->namespaced &&
        (jsx_kind == JSX_REACT || jsx_kind == JSX_REACT_JSX || jsx_kind == JSX_REACT_JSXDEV)
    ) {
        csview name = span_sv(my jsx->tag_name.inner_span);
        isize colon = csview_find(name, ":");
        csview ns = colon == c_NPOS ? name : csview_trim(csview_slice(name, 0, colon));
        bool intrinsic_like = ns.size && ((ns.buf[0] >= 'a' && ns.buf[0] <= 'z') || csview_contains(ns, "-"));
        if (!intrinsic_like) {
            error_ctx_grammar_error_d(errs, my jsx->tag_name.inner_span, 2639, "React components cannot include JSX namespace names");
        }
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
}

// Not inlined into expr_typecheck(), whose frame deeply nested expressions pay for (binderBinaryExpressionStress)
__attribute__((noinline)) static void jsx_typecheck(Expr const* me, ErrorCtx* errs) {
    switch (my tag) {
        case EXPR_JSX_ELEMENT:
            // An element is checked after the statements of its file (tsc's checkJsxElementDeferred): its type is
            // JSX.Element whatever its tag and attributes are
            if (current_checker->defer_checks) {
                checker_defer(current_checker, DEFERRED_JSX_ELEMENT, me);
                break;
            }
            // fallthrough
        case EXPR_JSX_FRAGMENT:
            jsx_element_typecheck(me, errs);
            break;

        case EXPR_JSX_ATTRIBUTES: {
            TyWrittenProp* written = nullptr;
            u32 written_count = 0;
            for_each (i, vec_jsx_attribute, &my jsx_attributes) {
                if (i->tag != JSX_ATTRIBUTE_NO_VALUE && i->tag != JSX_ATTRIBUTE_EMPTY_EXPR) {
                    expr_typecheck(&i->value, errs);
                }
                if (i->tag == JSX_ATTRIBUTE_SPREAD) {
                    // (Reduced: tsc's createJsxAttributesTypeFromAttributesProperty)
                    Ty const* type = ty_reduced(current_checker, expr_get_type(&i->value));
                    ty_check_spread_type(current_checker, type, expr_outer_span(&i->value), errs);
                    ty_check_spread_overrides(current_checker, type, written, written_count, i->span, errs);
                } else {
                    written = realloc(written, sizeof(TyWrittenProp) * (written_count + 1));
                    panic_if(!written, "out of memory");
                    written[written_count++] = (TyWrittenProp){ checker_atom(current_checker, span_sv(i->name)), i->span };
                }
            }
            free(written);
        }   break;

        default: impossible;
    }
}

// The binary case of expr_typecheck() once the left operand is checked
static void binary_expr_typecheck_rest(Expr const* me, bool root, ErrorCtx* errs) {
    if (my binary_op->op == BINARY_OP_AND_AND || my binary_op->op == BINARY_OP_OR_OR) {
        expr_check_truthiness(&my binary_op->left, errs);
        if (my binary_op->op == BINARY_OP_OR_OR) or_or_check_nullish_operand(me, errs);
    } else if (my binary_op->op == BINARY_OP_EH_EH) {
        nullish_coalesce_typecheck(me, errs);
    } else if (my binary_op->op == BINARY_OP_COMMA && !compiler_options()->allow_unreachable_code &&
        !my binary_op->indirect_call && !my binary_op->jsx_siblings && is_side_effect_free(&my binary_op->left)
    ) {
        error_ctx_error_d(errs, expr_error_span(&my binary_op->left), 2695,
            "Left side of comma operator is unused and has no side effects.");
    }
    logical_operand = binary_op_is_logical(my binary_op->op) ? &my binary_op->right : nullptr;
    expr_typecheck(&my binary_op->right, errs);
    logical_operand = nullptr;
    binary_op_typecheck(
        expr_get_type(&my binary_op->left),
        my binary_op->op,
        expr_get_type(&my binary_op->right),
        me,
        errs
    );
    // The top of a tree of logical operators (tsc's checkBinaryLikeExpression reports on each of them with the
    // parent of the tree in mind)
    if (root && is_logical_binary(me) && me != if_condition && compiler_options()->strict_null_checks) {
        CondPath path = { me, nullptr };
        check_known_truthy_tree(me, &path, false, (TruthyBody){0}, errs);
    }
}

// `module.exports = v` in JavaScript where v is nothing but null, undefined or never: the module's type is any, which
// tsc's getTypeOfFuncClassEnumModule reports at the assignment (getWidenedTypeForAssignmentDeclaration of `export=`
// through reportImplicitAny of a BinaryExpression, whose name is missing)
// Whether a `module.exports = v` of the file has a JSDoc `@type` (the module's export= has a typed declaration)
static bool module_exports_declared_type(Expr const* me) {
    SourceFile const* file = span_source(my inner_span);
    Symbol const* module = file && file->scope ? file->scope->container : nullptr;
    Symbol const* export_equals = module && module->exports ? symbol_table_get(module->exports, c_sv("export=")) : nullptr;
    for (u32 i = 0; export_equals && i < export_equals->decl_count; i++) {
        if (export_equals->decls[i].kind == SYMBOL_DECL_ASSIGNMENT && symbol_decl_jsdoc_type(&export_equals->decls[i])) return true;
    }
    return false;
}

static void js_module_exports_implicit_any_check(Expr const* me, ErrorCtx* errs) {
    SourceFile const* file = span_source(my inner_span);
    if (!file || !file->is_js || !compiler_options()->no_implicit_any) return;
    Symbol const* module = file->scope ? file->scope->container : nullptr;
    Symbol const* export_equals = module && module->exports ? symbol_table_get(module->exports, c_sv("export=")) : nullptr;
    if (!export_equals) return;
    Span left = expr_outer_span(&my binary_op->left);
    // (At the value declaration, the first)
    if (!export_equals->decl_count || export_equals->decls[0].name.buf != left.buf) return;
    Ty const* type = ty_type_of_symbol(current_checker, export_equals);
    // (Not never, which doesn't widen: `const ns: never = {}; module.exports = ns`)
    if (!ty_only_nullable(type) || (type->flags & (TYF_ANY | TYF_NEVER))) return;
    error_ctx_error_d(errs, my inner_span, 7008, "Member '(Missing)' implicitly has an 'any' type.");
}

// A JavaScript assignment declaration whose member's type is any because of what it assigns: TS7008 on `this.x = []`
// (any[]) or on the first declaration of a member only null or undefined are assigned (tsc's reportImplicitAny in
// getWidenedTypeForAssignmentDeclaration and getInitializerTypeFromAssignmentDeclaration)
static void js_assignment_implicit_any_check(Expr const* me, ErrorCtx* errs) {
    BinaryOpExpr const* op = my binary_op;
    if (op->op == BINARY_OP_EQUAL && expr_is_module_exports(&op->left)) {
        js_module_exports_implicit_any_check(me, errs);
        return;
    }
    if (op->op != BINARY_OP_EQUAL || op->left.tag != EXPR_PROPERTY || !property_rare(op->left.property)->declares) return;
    SourceFile const* file = span_source(my inner_span);
    if (!file || !compiler_options()->no_implicit_any) return;
    Symbol const* member = property_rare(op->left.property)->declares;
    SymbolDecl const* first = nullptr;
    SymbolDecl const* own = nullptr;
    for (u32 i = 0; i < member->decl_count; i++) {
        SymbolDecl const* decl = &member->decls[i];
        if (decl->kind != SYMBOL_DECL_ASSIGNMENT) continue;
        if (!first) first = decl;
        if (decl->node == (void const*)&op->right) own = decl;
    }
    if (!own || !own->node) return;
    int kind = ty_js_assignment_implicit_any(current_checker, member, own->node);
    if (!file->is_js && kind != 1) return; // (getWidenedTypeForAssignmentDeclaration's report is JavaScript's)
    if (kind == 1) {
        error_ctx_error_d(errs, my inner_span, 7008, "Member '", member->name, "' implicitly has an 'any[]' type.");
    } else if ((kind == 2 || kind == 3) && own == first) {
        // (At the value declaration, the assignment: tsgo's reportImplicitAny of a declaration that is a binary
        // expression)
        error_ctx_error_d(errs, my inner_span, 7008, "Member '", member->name, "' implicitly has an '", kind == 2 ? "any" : "any[]", "' type.");
    }
}

// JavaScript `/** @type {T} */ this.x = v` in a function, which declares nothing: tsgo only resolves the reparsed
// binary.Type when the value asks for its contextual type, never checks it as such (no TS2526 for `{this}`)
static bool is_undeclared_this_assignment(BinaryOpExpr const* op) {
    if (op->op != BINARY_OP_EQUAL || op->left.tag != EXPR_PROPERTY || property_rare(op->left.property)->declares) return false;
    IdentifierExpr const* this = expr_this_keyword(&op->left.property->expr);
    return this && this->this_kind == THIS_ANY && op->right.tag != EXPR_FUNCTION && op->right.tag != EXPR_OBJECT;
}

// The binary case of expr_typecheck(). A chain (`a + b + c + ...`) is walked down its left spine with a loop, not a
// recursion per operand: generated code has chains long enough to overflow the stack (tsc checks them iteratively too)
static void binary_expr_typecheck(Expr const* me, ErrorCtx* errs) {
    bool root = logical_operand != me;
    logical_operand = nullptr;
    if (my binary_op->jsdoc_type && !my binary_op->jsdoc_type_borrowed && !is_undeclared_this_assignment(my binary_op)) {
        type_typecheck(my binary_op->jsdoc_type, errs);
    }
    js_assignment_implicit_any_check(me, errs);
    if (my binary_op->op == BINARY_OP_IN && my binary_op->left.tag == EXPR_PRIVATE_NAME &&
        !expr_is_perenthisized(&my binary_op->left)
    ) {
        // The one place `#x` is an expression
        if (private_names_lowered()) {
            ty_check_emit_helpers(current_checker, expr_outer_span(&my binary_op->left), EMIT_HELPER_CLASS_PRIVATE_FIELD_IN, errs);
        }
        expr_typecheck(&my binary_op->right, errs);
        ty_check_in_expression(current_checker, &my binary_op->left, &my binary_op->right, errs);
        return;
    }
    u32 depth = 0;
    for (Expr const* e = me; e->binary_op->left.tag == EXPR_BINARY_OP; e = &e->binary_op->left) depth++;
    if (!depth) {
        expr_typecheck(&my binary_op->left, errs);
        binary_expr_typecheck_rest(me, root, errs);
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
    logical_operand = is_logical_binary(spine[depth - 1]) ? e : nullptr;
    expr_typecheck(e, errs); // The bottom binary expression, whose left operand isn't one
    logical_operand = nullptr;
    for (u32 i = depth; i-- > 0;) binary_expr_typecheck_rest(spine[i], i ? !is_logical_binary(spine[i - 1]) : root, errs);
    if (spine != local) free(spine);
}

// The callee of the call being checked, while it is (`import.x(...)`: tsc's checkGrammarMetaProperty's isCallee)
static thread_local Expr const* checked_callee;

// `import.meta` needs a module format that has it (tsc's checkImportMetaProperty)
static void import_meta_typecheck(Expr const* me, ErrorCtx* errs) {
    Span name = property_name(my property);
    if (csview_equals(span_sv(name), "defer") || csview_equals(span_sv(name), "source")) { // (tsgo's IsImportPhaseMetaProperty)
        // (Not called: `import.defer(...)` is a deferred import call, see parse_import_keyword_expr())
        Span end = span_make_in(my inner_span, span_end_ptr(my inner_span), span_end_ptr(my inner_span));
        error_ctx_grammar_error_d(errs, end, 1005, "'(' expected.");
        return;
    }
    if (!csview_equals(span_sv(name), "meta")) { // (tsc's checkGrammarMetaProperty)
        if (me == checked_callee) {
            error_ctx_grammar_error_d(errs, name, 18061, "'", name,
                "' is not a valid meta-property for keyword 'import'. Did you mean 'meta', 'defer', or 'source'?");
        } else {
            error_ctx_grammar_error_d(errs, name, 17012, "'", name, "' is not a valid meta-property for keyword 'import'. Did you mean 'meta'?");
        }
    }
    ModuleKind module = compiler_options()->module;
    if (module >= MODULE_KIND_NODE16 && module <= MODULE_KIND_NODENEXT) {
        if (source_file_implied_format_for_emit(span_source(my inner_span)) != MODULE_KIND_ESNEXT) {
            error_ctx_error_d(errs, my inner_span, 1470,
                "The 'import.meta' meta-property is not allowed in files which will build into CommonJS output.");
        }
    } else if (module < MODULE_KIND_ES2020) {
        error_ctx_error_d(errs, my inner_span, 1343, "The 'import.meta' meta-property is only allowed when the "
            "'--module' option is 'es2020', 'es2022', 'esnext', 'system', 'node16', 'node18', 'node20', or 'nodenext'.");
    }
}

// (Checker.const_enum_object_ok: the object of the property access being checked, where a const enum object may be
// used. Checker.type_only_site_depth: inside a type (a type query, a computed name of a type member) there's no const
// enum check at all, tsc's isValidTypeOnlyAliasUseSite, and a type query may name one)

// tsc's checkConstEnumAccess: a const enum object only as the object of a property or element access (TS2475), and
// with isolatedModules an ambient one not at all (TS2748)
static void check_const_enum_access(Expr const* me, Ty const* type, ErrorCtx* errs) {
    if (type->kind != TY_DECLARED || type->declared.kind != TY_DECLARED_ENUM_OBJECT || !type->declared.symbol ||
        !(type->declared.symbol->flags & SYMBOL_CONST_ENUM)
    ) {
        return;
    }
    if (current_checker->type_only_site_depth) return;
    bool object_ok = me == current_checker->const_enum_object_ok;
    if (!object_ok) {
        error_ctx_error_d(errs, expr_error_span(me), 2475, "'const' enums can only be used in property or index access "
            "expressions or the right hand side of an import declaration or export assignment or type query.");
    }
    if (!symbol_const_enum_is_ambient(type->declared.symbol)) return;
    // With isolatedModules as given (not implied by verbatimModuleSyntax), or verbatimModuleSyntax where the name
    // isn't an import: an import of one is checked at the import (tsc's checkConstEnumAccess, checkAliasSymbol)
    CompilerOptions const* options = compiler_options();
    bool isolated = options->is_set[COMPILER_OPTION_isolated_modules] && options->isolated_modules;
    if (!isolated && options->verbatim_module_syntax && object_ok) {
        Expr const* first = me;
        while (first->tag == EXPR_PROPERTY) first = &first->property->expr;
        Symbol const* symbol = first->tag == EXPR_IDENTIFIER ? first->identifier.symbol : nullptr;
        isolated = !(symbol && symbol->flags & SYMBOL_ALIAS);
    }
    if (isolated) {
        error_ctx_error_d(errs, expr_error_span(me), 2748, "Cannot access ambient const enums when '",
            options->verbatim_module_syntax ? "verbatimModuleSyntax" : "isolatedModules", "' is enabled.");
    }
}

void expr_typecheck_type_only_site(Expr const* me, ErrorCtx* errs) {
    current_checker->type_only_site_depth++;
    expr_typecheck(me, errs);
    current_checker->type_only_site_depth--;
}

void expr_typecheck_export_assignment(Expr const* me, ErrorCtx* errs) {
    Expr const* outer_ok = current_checker->const_enum_object_ok;
    current_checker->const_enum_object_ok = me;
    expr_typecheck(me, errs);
    current_checker->const_enum_object_ok = outer_ok;
}

// `...x` of an array literal or a call: what isn't an array has to be iterable. Not in a destructuring assignment,
// where `x` is assigned to (`[...x] = e`). The walk's, and a loop's unfinished flow analysis's (ty_flow_partial_reports())
void expr_spread_errors(Expr const* me, Ty const* operand, ErrorCtx* errs) {
    Checker* checker = current_checker;
    if (!expr_is_destructuring_target(my spread) && !(operand->flags & TYF_ANY) && !ty_extends_array(checker, operand)) {
        ty_iterated_type(checker, ITERATION_SPREAD, operand, expr_outer_span(my spread), errs);
    }
}

static bool is_no_property_code(unsigned code) {
    return code == 2339 || code == 2550 || code == 2551 || code == 2576 || code == 2812;
}

// What tsc's checkPropertyAccessExpressionOrQualifiedName and checkElementAccessExpression report about `me`, a
// property or element access, given its object's type (with a chain's optional marker removed). The walk's, and with
// `partial` a loop's unfinished flow analysis's (ty_flow_partial_reports()): only what the object's and key's types
// decide, nothing that asks for the access's own type (which is being computed then)
void expr_property_access_errors(Expr const* me, Ty const* expr_type, bool partial, ErrorCtx* errs) {
    Checker* checker = current_checker;
    if (partial) {
        // (A private name's access is the walk's: what it reports doesn't depend on flow)
        if (my property->form == PROPERTY_EXPR_DOT && span_valid(property_name(my property)) &&
            property_name(my property).buf[0] == '#'
        ) {
            return;
        }
    } else if (ty_check_private_name_access(checker, my property, expr_type, errs)) {
        return;
    }
    bool error_if_not_found = my property->form == PROPERTY_EXPR_DOT;
    // `null.x`, `undefined[0]`, `u.x` with `u: undefined` (tsc's checkNonNullExpression; a union with null
    // or undefined is check_access()'s)
    if ((expr_type->flags & TYF_NULLABLE) && expr_type->kind != TY_UNION) {
        ty_check_non_null_type(checker, expr_type, &my property->expr, errs);
        return;
    }
    // An object that may be nullish indexed by a generic key (check_access() doesn't look at those): TS18048
    // and then its non-nullable part (tsc's checkNonNullExpression, type parameters by their constraint)
    // (A type parameter without constraint isn't: tsc says its members are missing instead)
    bool generic_key = my property->form == PROPERTY_EXPR_BRACKET && compiler_options()->strict_null_checks &&
        (expr_type->kind == TY_UNION || (expr_type->kind == TY_TYPE_PARAM && ty_base_constraint(checker, expr_type))) &&
        ty_is_generic_index(ty_regular(checker, ty_access_key_type(checker, my property)));
    // (tsc's getTypeFacts(IsUndefinedOrNull): an unconstrained type parameter is unknown's facts, which don't
    // have it, `x[k]` of `x: T | U` indexes `T | U`)
    if (generic_key && !my property->qualified_name && ty_undefined_or_null_facts(checker, expr_type)) {
        ty_check_non_null_type(checker, expr_type, &my property->expr, errs);
        expr_type = ty_non_nullable(checker, expr_type);
    }
    if (my property->qualified_name && (my property->form == PROPERTY_EXPR_DOT || my property->index.tag == EXPR_LITERAL) &&
        ty_namespace_type_only_prop(checker, expr_type, ty_literal_name(checker, ty_access_key_type(checker, my property)))
    ) {
        return;
    }
    // (Written to or called, the object is widened: tsc's checkPropertyAccessExpressionOrQualifiedName. What a
    // property access lacks is said about the type as it was)
    Ty const* unwidened = expr_type;
    if (my property->method_call || my property->assignment_target) {
        expr_type = ty_widened_objects(checker, ty_widened_nullable(checker, expr_type));
    }
    ErrorCtx found = error_ctx_scratch(errs);
    PropertyExpr const* saved_suggestion = checker->suggestion_access;
    checker->suggestion_access = my property->form == PROPERTY_EXPR_DOT && !my property->qualified_name ? my property : nullptr;
    ty_check_property_access(
        checker,
        expr_type,
        ty_access_key_type(checker, my property),
        expr_outer_span(&my property->expr),
        property_key_span(my property),
        // (In a `typeof a.b.c` type the object is a qualified name, an entity name expression only when it's an
        // identifier, `this` included: tsc's isEntityNameExpression)
        my property->qualified_name ?
            my property->expr.tag == EXPR_IDENTIFIER && !expr_is_perenthisized(&my property->expr) :
            expr_is_entity_name(&my property->expr),
        error_if_not_found,
        &found
    );
    Span name_span = property_key_span(my property);
    if (unwidened != expr_type && error_if_not_found && (error_ctx_find(&found, name_span, 2339) ||
        error_ctx_find(&found, name_span, 2550) || error_ctx_find(&found, name_span, 2551))
    ) {
        error_ctx_clear(&found);
        found.phase = errs->phase;
        ty_report_no_property(checker, unwidened, ty_access_key_type(checker, my property),
            property_key_span(my property), &found);
    }
    checker->suggestion_access = saved_suggestion;
    // What says the property is missing is said once, the first time (tsc's reportNonexistentProperty)
    bool no_property = false;
    for (intptr_t i = 0; i < vec_error_size(&found.errors); i++) {
        Error const* error = vec_error_at(&found.errors, i);
        no_property |= is_no_property_code(error->ts_code) && span_eq(error->span, name_span);
    }
    if (no_property) {
        if (ty_flow_partial_no_property(checker, my property)) {
            intptr_t kept = 0;
            for (intptr_t i = 0; i < vec_error_size(&found.errors); i++) {
                Error* error = vec_error_at_mut(&found.errors, i);
                if (is_no_property_code(error->ts_code) && span_eq(error->span, name_span)) {
                    error_drop(error);
                } else {
                    if (kept != i) found.errors.data[kept] = *error;
                    kept++;
                }
            }
            found.errors.size = kept;
        } else if (partial) {
            ty_flow_partial_no_property_mark(checker, my property);
        }
    }
    error_ctx_merge(errs, &found);
    error_ctx_drop(&found);
    ty_check_element_implicit_any(checker, my property, expr_type, my inner_span, errs);
    if (!partial) {
        // (Unless the flow type replaced the deferred `O[K]` by its constraint: tsc's checkIndexedAccessIndexType)
        if (ty_expr_type(checker, me)->flags & TYF_ERROR) {
            ty_check_generic_element_access(checker, my property, expr_type, my inner_span, errs);
        }
        ty_check_generic_index_write(checker, my property, expr_type, my inner_span, errs);
        ty_check_member_modifiers(checker, my property, expr_type, my inner_span, errs);
    }
    if (compiler_options()->no_property_access_from_index_signature) {
        ty_check_index_signature_dot_access(checker, my property, expr_type, errs);
    }
    if (!partial) ty_check_property_used_before_assigned(checker, me, errs);
}

void expr_typecheck(Expr const* me, ErrorCtx* errs) {
    // The expression's own type first: evaluating it is what gives the expressions inside it their contextual types
    // (whoever knows this expression's contextual type has done the same before getting here)
    Ty const* own_type = expr_get_type((Expr*)me);
    if ((my tag == EXPR_IDENTIFIER || my tag == EXPR_PROPERTY) && (own_type->flags & TYF_OBJECT)) {
        check_const_enum_access(me, own_type, errs);
    }
    switch (my tag) {
        case EXPR_IMPLICIT_ANY: break;
        case EXPR_LITERAL:
            // (tsc's checkGrammarBigIntLiteral: expressions only, not literal types, and not in an ambient context)
            if (my literal.tag == VALUE_BIGINT && compiler_options()->target < SCRIPT_TARGET_ES2020 && !my literal_ambient) {
                error_ctx_grammar_error_d(errs, my inner_span, 2737,
                    "BigInt literals are not available when targeting lower than ES2020.");
            }
            break;

        case EXPR_IDENTIFIER: identifier_expr_typecheck(&my identifier, errs); break;

        case EXPR_BINARY_OP: binary_expr_typecheck(me, errs); break;

        case EXPR_UNARY_OP:
            if (my unary_op->outside_generator) { // TS1163 (from the resolve walk), the operand checked anyway
                expr_typecheck(&my unary_op->expr, errs);
                break;
            }
            // (`void`'s operand is checked after the statements of its file, tsc's checkVoidExpression)
            if (my unary_op->op == UNARY_OP_VOID && current_checker->defer_checks) {
                checker_defer(current_checker, DEFERRED_EXPR, &my unary_op->expr);
                break;
            }
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
                    break;
                case UNARY_OP_AWAIT:
                    ty_check_await_operand(current_checker, expr_get_type(&my unary_op->expr), my inner_span, errs);
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
            Expr const* outer_ok = current_checker->const_enum_object_ok;
            current_checker->const_enum_object_ok = &my property->expr;
            expr_typecheck(&my property->expr, errs);
            current_checker->const_enum_object_ok = outer_ok;
            if (my property->form == PROPERTY_EXPR_BRACKET) expr_typecheck(&my property->index, errs);
            // (`a?.b` of `a?.b.c` without the undefined of a chain that ended)
            Ty const* expr_type = ty_optional_expression_type(current_checker, expr_get_type(&my property->expr),
                my property->optional_chain, nullptr);
            if (my property->form == PROPERTY_EXPR_DOT && span_valid(property_name(my property)) &&
                property_name(my property).buf[0] == '#'
            ) {
                private_access_check_emit_helpers(me, errs);
            }
            // (An incomplete never of control flow, silent: tsc's checkPropertyAccessExpressionOrQualifiedName returns it
            // before any check)
            if (expr_type != ty_intrinsic(current_checker, TY_SILENT_NEVER)) expr_property_access_errors(me, expr_type, false, errs);
        }   break;

        case EXPR_GENERIC_PARAMS: {
            Expr const* inner = &my generic_params->expr;
            if (inner->tag == EXPR_IDENTIFIER && inner->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT) {
                // `import<T>` (tsc's checkGrammarExpressionWithTypeArguments: at the whole expression, which is all).
                // A class's `extends import<T>` is the parser's, with the other heritage clause grammar
                if (my generic_params->heritage) break;
                Span whole = span_make_in(my inner_span, span_start_ptr(inner->inner_span), span_end_ptr(my inner_span));
                error_ctx_grammar_error_d(errs, whole, 1326, "This use of 'import' is invalid. 'import()' calls can be "
                    "written, but they must have parentheses and cannot have type arguments.");
                break;
            }
            generic_params_expr_typecheck(my generic_params, errs);
        }   break;

        case EXPR_AS_TYPE:
            // (tsc's checkAssertion: from the `<` to where the expression's trivia starts)
            if (my as_type->angle_bracket_syntax && compiler_options()->erasable_syntax_only) {
                error_ctx_error_d(errs, my as_type->keyword_span, 1294, "This syntax is not allowed when 'erasableSyntaxOnly' is enabled.");
            }
            if (as_type_is_const_assertion(my as_type) && !is_valid_const_assertion_argument(&my as_type->expr)) {
                error_ctx_error_d(errs, expr_outer_span(&my as_type->expr), 1355, "A 'const' assertion can only be applied to "
                    "references to enum members, or string, number, boolean, array, or object literals.");
            }
            if (!my as_type->is_satisfies && !as_type_is_const_assertion(my as_type)) {
                // The asserted type is the contextual type of the operand, which nobody has looked at yet
                Ty const* asserted = ty_of_node(&my as_type->type);
                ty_check_expr(current_checker, &my as_type->expr, asserted);
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
            // (After the branch is checked, see expr_check_if_known_truthy())
            if (compiler_options()->strict_null_checks) {
                CondPath path = { &my ternary->condition, nullptr };
                check_known_truthy(&my ternary->condition, &path, (TruthyBody){ .expr = &my ternary->yes }, errs);
            }
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
            Expr const* outer_callee = checked_callee;
            checked_callee = !my call->uses_new && !expr_is_perenthisized(callee) ? callee : nullptr;
            expr_typecheck(callee, errs);
            checked_callee = outer_callee;
            // Resolving the call gives the arguments their contextual types, so it comes before checking them
            Checker* checker = current_checker;
            LazyMemo* memo = lazy_type_state_peek((LazyType*)&my lazy_type);
            Ty const* contextual = memo && memo->done ? lazy_memo_context(memo) : nullptr;
            Ty const* callee_type = ty_expr_type(checker, callee);
            // `f?.()` doesn't call the undefined, and neither does `a?.b()` the undefined of a chain that ended
            callee_type = ty_optional_expression_type(checker, callee_type, my call->optional_chain, nullptr);
            // (tsc's checkNonNullExpression of the callee, for `new` too: unknown is TS18046, then the error type)
            if ((callee_type->flags & TYF_UNKNOWN) && !my call->is_extends && compiler_options()->strict_null_checks) {
                ty_check_non_null_type(checker, callee_type, callee, errs);
                callee_type = ty_intrinsic(checker, TY_ERROR);
            }
            if (!my call->uses_new && !my call->is_extends) {
                callee_type = ty_check_non_null_callee(checker, callee_type, expr_outer_span(callee), errs);
            } else if (my call->uses_new && !my call->is_extends) {
                // (tsc's resolveNewExpression: checkNonNullExpression, `new o.m()` with an optional m is TS18048)
                Ty const* non_null = ty_check_non_null_type(checker, callee_type, callee, errs);
                callee_type = non_null ? non_null : ty_intrinsic(checker, TY_ERROR);
            }
            // `import("m", options)`: the options are ImportCallOptions
            if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT &&
                vec_expr_size(&my call->args) > 0 && vec_expr_at(&my call->args, 0)->tag != EXPR_SPREAD
            ) {
                ty_check_import_call_specifier(checker, vec_expr_at(&my call->args, 0), errs);
            }
            if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT &&
                vec_expr_size(&my call->args) > 1
            ) {
                ty_check_import_call_options(checker, vec_expr_at(&my call->args, 1), errs);
            }
            ty_call_typecheck(checker, callee_type, my call, contextual, my inner_span, errs);
            if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT) {
                import_call_check_grammar(me, errs);
            }
            for_each (i, vec_expr, &my call->args) {
                expr_typecheck(i, errs);
                // A spread argument is checked as iterable even when never, which every array type takes (tsc's
                // checkSpreadExpression; an array literal's spread elements go by isArrayLikeType instead)
                if (i->tag == EXPR_SPREAD && (expr_get_type(i->spread)->flags & TYF_NEVER)) {
                    ty_iterated_type(checker, ITERATION_SPREAD, expr_get_type(i->spread), expr_outer_span(i->spread), errs);
                }
            }
        }   break;

        case EXPR_FUNCTION:
            function_impl_typecheck(my function, errs);
            break;

        case EXPR_OBJECT: {
            object_bind_members(me, errs);
            object_check_grammar(me, errs);
            ty_check_object_literal_pattern(current_checker, me, errs);
            ty_check_object_literal_late_members(current_checker, me, errs);
            // (The names written before a spread are what it may override: none to collect without one)
            bool has_spread = false;
            for_each (i, vec_object_expr_item, &my object) has_spread |= i->tag == OBJECT_ITEM_SPREAD;
            TyWrittenProp* written = nullptr;
            u32 written_count = 0;
            for_each (i, vec_object_expr_item, &my object) {
                // A method or accessor of a destructuring assignment target is no target: the error and nothing else
                // (tsgo's checkObjectLiteralDestructuringPropertyAssignment, which doesn't check it as an expression)
                if (my assignment_pattern &&
                    (i->tag == OBJECT_ITEM_METHOD || i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER)
                ) {
                    error_ctx_error_d(errs, i->name_span, 1136, "Property assignment expected.");
                    continue;
                }
                // (tsc's checkGrammarObjectLiteralExpression, of a property assignment, reported whatever the file's
                // parse errors: addErrorOrSuggestion)
                if (i->tag == OBJECT_ITEM_PROPERTY && !i->computed_key && !my assignment_pattern &&
                    span_is_bigint_name(i->name_span, i->name_is_value)
                ) {
                    error_ctx_error_d(errs, i->name_span, 1539, "A 'bigint' literal cannot be used as a property name.");
                }
                if (has_spread && i->tag != OBJECT_ITEM_SPREAD && !i->computed_key) {
                    written = realloc(written, sizeof(TyWrittenProp) * (written_count + 1));
                    panic_if(!written, "out of memory");
                    written[written_count++] = (TyWrittenProp){ checker_atom(current_checker, cstr_sv(&i->name_string)), i->span };
                }
                if (i->computed_key) {
                    expr_typecheck(i->computed_key, errs);
                    ty_check_computed_name(current_checker, i->computed_key, i->name_span, false, errs);
                }
                if (object_item_rare(i)->shorthand_default && !(i->value.tag == EXPR_IDENTIFIER && i->value.identifier.assignment_target)) {
                    error_ctx_grammar_error_d(errs, object_item_rare(i)->shorthand_equals, 1312,
                        "Did you mean to use a ':'? An '=' can only follow a property name when the containing object literal is part of a destructuring pattern.");
                }
                if (object_item_is_private_name(i) && my assignment_pattern && i->tag == OBJECT_ITEM_PROPERTY) {
                    // (tsgo's checkObjectLiteralDestructuringPropertyAssignment; checkGrammarObjectLiteralExpression
                    // doesn't look at a destructuring target)
                    error_ctx_grammar_error_d(errs, i->name_span, 18064, "Private identifiers cannot be used in destructuring patterns.");
                } else if (object_item_is_private_name(i)) {
                    // tsc's checkGrammarObjectLiteralExpression; a method's is checkMethodDeclaration's (at its name, no
                    // grammar error), an accessor's TS18028 checkGrammarAccessor's
                    char const* message = "Private identifiers are not allowed outside class bodies.";
                    if (i->tag == OBJECT_ITEM_PROPERTY || i->tag == OBJECT_ITEM_SHORTHAND) {
                        error_ctx_grammar_error_d(errs, i->name_span, 18016, message);
                    } else {
                        error_ctx_error_d(errs, i->name_span, 18016, message);
                    }
                }
                if ((i->tag == OBJECT_ITEM_METHOD || i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER) &&
                    i->value.tag == EXPR_FUNCTION && !i->value.function->has_body && !i->value.function->body_missing &&
                    !i->modifiers_failed
                ) {
                    // tsc's checkGrammarMethod/checkGrammarAccessor: at the member's last character
                    char const* end = span_end_ptr(i->span);
                    while (end > span_start_ptr(i->span) && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) end--;
                    error_ctx_grammar_error_d(errs, span_make_in(i->span, end - 1, end), 1005, "'{' expected.");
                }
                expr_typecheck(&i->value, errs);
                // tsc's getTypeOfAccessors: a setter without a parameter annotation or a getter to take the type from is
                // implicitly any
                if (i->tag == OBJECT_ITEM_SETTER && i->value.tag == EXPR_FUNCTION && compiler_options()->no_implicit_any &&
                    span_source(i->name_span) && !object_item_rare(i)->jsdoc_type
                ) {
                    FunctionArg const* value = nullptr;
                    for_each (arg, vec_function_arg, &i->value.function->args) {
                        Binding const* binding = &arg->initializer.binding;
                        if (binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) continue;
                        value = arg;
                        break;
                    }
                    if (value && !value->initializer.binding.has_explicit_type && !initialized_binding_getter(&value->initializer)) {
                        error_ctx_error_d(errs, i->name_span, 7032, "Property '", i->name_span,
                            "' implicitly has type 'any', because its set accessor lacks a parameter type annotation.");
                    }
                }
                // tsc's checkAccessorDeclaration: a getter and setter pair agree on `abstract` (TS2676), and the getter is
                // at least as accessible as the setter (TS2808), at both names. Their modifiers are TS1042 in an object
                // literal, but the declarations still have them
                if (i->tag == OBJECT_ITEM_SETTER && !i->computed_key && span_source(i->name_span)) {
                    ObjectExprItem const* getter = nullptr;
                    for_each (j, vec_object_expr_item, &my object) {
                        if (j->tag == OBJECT_ITEM_GETTER && !j->computed_key && cstr_equals_sv(&j->name_string, cstr_sv(&i->name_string))) {
                            getter = j;
                            break;
                        }
                    }
                    if (getter && getter->abstract_modifier != i->abstract_modifier) {
                        error_ctx_error_d(errs, getter->name_span, 2676, "Accessors must both be abstract or non-abstract.");
                        error_ctx_error_d(errs, i->name_span, 2676, "Accessors must both be abstract or non-abstract.");
                    }
                    if (getter && ((getter->access_modifier == 1 && i->access_modifier == 0) ||
                        (getter->access_modifier == 2 && i->access_modifier != 2))
                    ) {
                        error_ctx_error_d(errs, getter->name_span, 2808, "A get accessor must be at least as accessible as the setter");
                        error_ctx_error_d(errs, i->name_span, 2808, "A get accessor must be at least as accessible as the setter");
                    }
                }
                // (A getter without body or annotation and no setter, which only parser recovery makes: TS7033)
                if (i->tag == OBJECT_ITEM_GETTER && i->value.tag == EXPR_FUNCTION && compiler_options()->no_implicit_any &&
                    span_source(i->name_span) && !object_item_rare(i)->jsdoc_type && !i->value.function->ret_explicit &&
                    !i->value.function->has_body && !i->value.function->body_missing && !i->computed_key
                ) {
                    bool has_setter = false;
                    for_each (j, vec_object_expr_item, &my object) {
                        has_setter |= j->tag == OBJECT_ITEM_SETTER && !j->computed_key && cstr_equals_sv(&j->name_string, cstr_sv(&i->name_string));
                    }
                    if (!has_setter) {
                        error_ctx_error_d(errs, i->name_span, 7033, "Property '", i->name_span,
                            "' implicitly has type 'any', because its get accessor lacks a return type annotation.");
                    }
                }
                if (object_item_rare(i)->jsdoc_type) {
                    type_typecheck(object_item_rare(i)->jsdoc_type, errs);
                    // (At the property assignment, tsc's getErrorSpanForNode of it: a method's is its name)
                    // (What the property holds: tsc's checkPropertyAssignment, checkExpressionForMutableLocation, with the
                    // tag's type as the contextual type: `a: "a"` is a string against `@type {42}`)
                    Ty const* jsdoc = ty_of_node(object_item_rare(i)->jsdoc_type);
                    Ty const* held = i->tag == OBJECT_ITEM_METHOD ? expr_get_type(&i->value) :
                        ty_mutable_location_type(current_checker, &i->value, jsdoc);
                    ty_check_assignable_expr(current_checker, held, jsdoc,
                        i->tag == OBJECT_ITEM_METHOD ? i->name_span : i->span, &i->value, errs);
                }
                if (i->tag == OBJECT_ITEM_SPREAD) {
                    // `{ ...o }` is emitted with __assign below ES2015, `({ ...x } = o)` with __rest below ES2018
                    ScriptTarget target = compiler_options()->target;
                    if (my assignment_pattern || expr_is_destructuring_target(&i->value)) {
                        if (target < SCRIPT_TARGET_ES2018) ty_check_emit_helpers(current_checker, i->span, EMIT_HELPER_REST, errs);
                    } else {
                        // (Reduced: tsc's checkObjectLiteral, `{ ...x }` with x an intersection that is never)
                        Ty const* type = ty_reduced(current_checker, expr_get_type(&i->value));
                        ty_check_spread_type(current_checker, type, i->span, errs);
                        ty_check_spread_overrides(current_checker, type, written, written_count, i->span, errs);
                    }
                }
            }
            free(written);
        }   break;

        case EXPR_ARRAY:
            for_each (i, vec_expr, &my array) {
                // (A destructuring rest element with an initializer is TS1186 and not checked any further: tsc's
                // checkArrayLiteralDestructuringElementAssignment returns there, see assignment_target_typecheck())
                if (my assignment_pattern && i->tag == EXPR_SPREAD && i->spread->tag == EXPR_BINARY_OP &&
                    i->spread->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(i->spread)
                ) {
                    continue;
                }
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
            expr_spread_errors(me, expr_get_type(my spread), errs);
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
                // (Explicit type arguments are the call's, as for a call: not an instantiation expression)
                Expr const* tag = my template_->tag;
                if (tag->tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(tag)) {
                    for_each (i, vec_spanned_type, &tag->generic_params->params) type_typecheck(&i->type, errs);
                    tag = &tag->generic_params->expr;
                }
                expr_typecheck((Expr*)tag, errs);
                // Resolving the call gives the substitutions their contextual types, so it comes before checking them
                Checker* checker = current_checker;
                LazyMemo* memo = lazy_type_state_peek((LazyType*)&my lazy_type);
                Ty const* contextual = memo && memo->done ? lazy_memo_context(memo) : nullptr;
                ty_tagged_template_typecheck(checker, ty_expr_type(checker, tag), me, contextual, errs);
            }
            // (An untyped tag call checks the template as an untagged one: tsc's resolveUntypedCall)
            bool untagged = !my template_->tag;
            if (!untagged) {
                Expr const* tag = my template_->tag;
                if (tag->tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(tag)) tag = &tag->generic_params->expr;
                untagged = (ty_expr_type(current_checker, tag)->flags & (TYF_ANY | TYF_ERROR)) != 0;
            }
            for_each (i, vec_expr, &my template_->substitutions) {
                expr_typecheck(i, errs);
                // tsc's checkTemplateExpression
                if (untagged && maybe_kind(ty_expr_type(current_checker, i), TYF_ES_SYMBOL_LIKE, true)) {
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

        // Like `||` (tsc 6's Coalesce = LogicalOR): `a ?? b || c` is `(a ?? b) || c`, an error anyway (TS5076)
        case BINARY_OP_EH_EH:
            return 30;

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

const ObjectExprItemRare object_item_no_rare = {0};

ObjectExprItemRare* object_item_rare_mut(ObjectExprItem* me) {
    if (!my rare) my rare = ast_new(ObjectExprItemRare, {0});
    return my rare;
}

void object_expr_item_drop(ObjectExprItem* me) {
    ast_cstr_drop(&my name_string);
    if (my computed_key) {
        expr_drop(my computed_key);
        ast_delete(my computed_key);
    }
    if (my rare && my rare->shorthand_default) {
        expr_drop(my rare->shorthand_default);
        ast_delete(my rare->shorthand_default);
    }
    if (my rare && my rare->jsdoc_type) {
        type_drop(my rare->jsdoc_type);
        ast_delete(my rare->jsdoc_type);
    }
    ast_delete(my rare);
    expr_drop(&my value);
}

bool expr_is_plain_identifier(Expr const* me, const char* name) {
    return my tag == EXPR_IDENTIFIER && !my identifier.keyword && !expr_is_perenthisized(me) &&
        csview_equals(span_sv(my inner_span), name);
}

// `a.b` or `a["b"]`: the name b, else an invalid span
Span expr_static_property_name(Expr const* me) {
    if (my tag != EXPR_PROPERTY || expr_is_perenthisized(me) || my property->optional_chain) return span_null;
    if (my property->form == PROPERTY_EXPR_DOT) return property_name(my property);
    Expr const* prop = &my property->index;
    if (prop->tag == EXPR_LITERAL && prop->literal.tag == VALUE_NUMBER) {
        // `o[77]` (tsc's isStringOrNumericLiteralLike): named by the number's text, when that is how it prints. Also
        // `o[(77)]`: tsgo's GetElementOrPropertyAccessName skips parentheses
        csview digits = span_sv(prop->inner_span);
        bool canonical = digits.size && (digits.buf[0] != '0' || digits.size == 1);
        for (isize i = 0; i < digits.size && canonical; i++) canonical = digits.buf[i] >= '0' && digits.buf[i] <= '9';
        return canonical && digits.size < 16 ? prop->inner_span : span_null;
    }
    if (prop->tag != EXPR_LITERAL || prop->literal.tag != VALUE_STRING) return span_null;
    // Without the quotes
    Span ret = prop->inner_span;
    if (ret.size < 2) return span_null;
    ret.buf++;
    ret.size -= 2;
    return ret;
}

IdentifierExpr const* expr_this_keyword(Expr const* me) {
    if (my tag != EXPR_IDENTIFIER || expr_is_perenthisized(me)) return nullptr;
    return my identifier.keyword == KEYWORD_IDENTIFIER_THIS ? &my identifier : nullptr;
}

// tsc's isValidConstAssertionArgument (parentheses are transparent)
static bool is_valid_const_assertion_argument(Expr const* me) {
    switch (my tag) {
        case EXPR_LITERAL:
            return my literal.tag == VALUE_NUMBER || my literal.tag == VALUE_STRING || my literal.tag == VALUE_BOOLEAN ||
                my literal.tag == VALUE_BIGINT;
        case EXPR_ARRAY: case EXPR_OBJECT: return true;
        case EXPR_TEMPLATE: return !my template_->tag;
        case EXPR_UNARY_OP: {
            Expr const* operand = &my unary_op->expr;
            if (expr_is_perenthisized(operand) || operand->tag != EXPR_LITERAL) return false;
            return (my unary_op->op == UNARY_OP_MINUS && (operand->literal.tag == VALUE_NUMBER || operand->literal.tag == VALUE_BIGINT)) ||
                (my unary_op->op == UNARY_OP_PLUS && operand->literal.tag == VALUE_NUMBER);
        }
        case EXPR_PROPERTY: {
            Expr object = my property->expr;
            object.paren_before = object.paren_after = 0; // (skipParentheses)
            Symbol const* symbol = expr_is_entity_name(&object) ? expr_entity_name_symbol(&object) : nullptr;
            return symbol && (symbol->flags & SYMBOL_ENUM);
        }
        default: return false;
    }
}

Symbol const* expr_entity_name_symbol(Expr const* me) {
    if (expr_is_perenthisized(me)) return nullptr;
    if (my tag == EXPR_IDENTIFIER) return my identifier.keyword ? nullptr : my identifier.target;
    if (my tag != EXPR_PROPERTY) return nullptr;
    Span name = expr_static_property_name(me);
    Symbol const* object = span_valid(name) ? expr_entity_name_symbol(&my property->expr) : nullptr;
    if (!object || object->is_module || (object->flags & SYMBOL_ALIAS) || !object->exports) return nullptr;
    return symbol_table_get(object->exports, span_sv(name));
}

bool expr_is_module_exports(Expr const* me) {
    Span name = expr_static_property_name(me);
    return span_valid(name) && csview_equals(span_sv(name), "exports") && expr_is_plain_identifier(&my property->expr, "module");
}

CommonJsTarget expr_commonjs_target(Expr const* left, Span* name_ret) {
    if (expr_is_module_exports(left)) return COMMONJS_TARGET_MODULE_EXPORTS;
    Span name = expr_static_property_name(left);
    // (`exports.#x` is no export: tsc's isBindableStaticAccessExpression wants an identifier)
    if (!span_valid(name) || (name.size && name.buf[0] == '#')) return COMMONJS_TARGET_NONE;
    Expr const* object = &left->property->expr;
    if (!expr_is_plain_identifier(object, "exports") && !expr_is_module_exports(object)) return COMMONJS_TARGET_NONE;
    if (name_ret) *name_ret = name;
    return COMMONJS_TARGET_EXPORTS_PROPERTY;
}

bool expr_symbol_is_commonjs_require(Symbol const* symbol) {
    if (!symbol || symbol == symbol->binder->require_symbol) return symbol != nullptr;
    return !(symbol->flags & SYMBOL_ALIAS) && (symbol->flags & (SYMBOL_FUNCTION | SYMBOL_VARIABLE)) && symbol->is_ambient;
}

bool expr_is_define_property_call(Expr const* me, Span* name_ret) {
    if (my tag != EXPR_CALL || my call->uses_new || my call->optional_chain || vec_expr_size(&my call->args) != 3) return false;
    Expr const* callee = &my call->callable;
    if (callee->tag != EXPR_PROPERTY || callee->property->form != PROPERTY_EXPR_DOT || callee->property->optional_chain ||
        !span_equals(property_name(callee->property), "defineProperty") ||
        !expr_is_plain_identifier(&callee->property->expr, "Object")
    ) {
        return false;
    }
    Expr const* key = vec_expr_at(&my call->args, 1);
    if (key->tag != EXPR_LITERAL || expr_is_perenthisized(key)) return false;
    Span text = key->inner_span;
    Span name;
    if (key->literal.tag == VALUE_STRING) {
        if (text.size < 2 || (isize)cstr_size(&key->literal.string) != text.size - 2) return false;
        name = span_make_in(text, text.buf + 1, text.buf + text.size - 1);
    } else if (key->literal.tag == VALUE_NUMBER && value_is_numeric_literal_name(span_sv(text))) {
        // (A numeric key too, tsc's isStringOrNumericLiteralLike: named by its text when that is the number's own, `42`)
        name = text;
    } else {
        return false;
    }
    Expr const* target = vec_expr_at(&my call->args, 0);
    if (!expr_is_entity_name(target)) return false;
    if (name_ret) *name_ret = name;
    return true;
}

bool expr_define_property_readonly(Expr const* call) {
    Expr const* descriptor = vec_expr_at(&call->call->args, 2);
    if (descriptor->tag != EXPR_OBJECT) return false;
    ObjectExprItem const* value = nullptr;
    ObjectExprItem const* writable = nullptr;
    bool setter = false;
    for_each (i, vec_object_expr_item, &descriptor->object) {
        if (i->tag == OBJECT_ITEM_SPREAD || i->computed_key) continue;
        csview name = cstr_sv(&i->name_string);
        if (csview_equals(name, "value")) value = i;
        if (csview_equals(name, "writable")) writable = i;
        if (csview_equals(name, "set")) setter = true;
    }
    if (!value) return !setter;
    if (!writable) return true;
    // (`writable: false`: tsc looks at the initializer's own type)
    Expr const* flag = &writable->value;
    return writable->tag == OBJECT_ITEM_PROPERTY && flag->tag == EXPR_LITERAL && flag->literal.tag == VALUE_BOOLEAN &&
        !flag->literal.boolean;
}

bool expr_is_exports_object(Expr const* me) {
    return expr_is_plain_identifier(me, "exports") || expr_is_module_exports(me);
}

bool expr_is_require_call(Expr const* me) {
    return my tag == EXPR_CALL && !my call->uses_new && vec_expr_size(&my call->args) == 1 &&
        expr_is_plain_identifier(&my call->callable, "require");
}

CommonJsTarget binary_commonjs_declaration(BinaryOpExpr const* assignment, Span* name_ret) {
    if (assignment->op != BINARY_OP_EQUAL) return COMMONJS_TARGET_NONE;
    SourceFile const* file = span_source(assignment->left.inner_span);
    if (!file || !file->is_commonjs) return COMMONJS_TARGET_NONE;
    return expr_commonjs_target(&assignment->left, name_ret);
}

bool computed_key_early_name(Expr const* key, cstr* out) {
    if (!key || expr_is_perenthisized(key)) return false;
    Expr const* literal = key;
    char const* sign = nullptr;
    if (key->tag == EXPR_UNARY_OP && (key->unary_op->op == UNARY_OP_MINUS || key->unary_op->op == UNARY_OP_PLUS)) {
        literal = &key->unary_op->expr;
        if (expr_is_perenthisized(literal) || literal->tag != EXPR_LITERAL || literal->literal.tag != VALUE_NUMBER) return false;
        sign = key->unary_op->op == UNARY_OP_MINUS ? "-" : "+";
    }
    if (literal->tag != EXPR_LITERAL || (literal->literal.tag != VALUE_STRING && literal->literal.tag != VALUE_NUMBER)) return false;
    if (!out) return true;
    if (sign) cstr_append(out, sign);
    if (literal->literal.tag == VALUE_STRING) cstr_append_s(out, literal->literal.string);
    else cstr_append_value_js(out, &literal->literal);
    return true;
}
