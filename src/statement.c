#include "statement.h"
#include "source_file.h"
#include "ty/ty.h"
#include "several_types.h"
#include "port_statement.h"
#include "error.h"
#include "scope.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "function_impl.h"
#include "program.h"
#include "flow.h"
#include "unused.h"
#include "module_resolution.h"

// Variable initialization

void variable_statement_drop(VariableStatement* me) {
    vec_initialized_binding_drop(&my initializers);
}

void variable_statement_add_names(VariableStatement* me, Scope* scope, bool exported, bool declared) {
    for_each (i, vec_initialized_binding, &my initializers) {
        initialized_binding_add_names(i, scope, (BindingAddNamesProps){
            .abstract_literals = !variable_statement_is_const(me),
            .exported = exported,
            .function_scoped = my keyword == VAR_INIT_VAR,
            .is_const = variable_statement_is_const(me),
            .is_ambient = declared,
        });
    }
}

void variable_statement_resolve_names(VariableStatement* me, Scope* names, ErrorCtx* errs) {
    for_each (i, vec_initialized_binding, &my initializers) {
        initialized_binding_lookup_names(i, names, errs);
    }
}

// The grammar of `using` declarations: tsc's checkGrammarVariableDeclarationList(), checkAwaitGrammar() and the using
// part of checkGrammarVariableDeclaration()
static void variable_statement_check_using(VariableStatement* me, bool for_in, ErrorCtx* errs) {
    bool is_await = my keyword == VAR_INIT_AWAIT_USING;
    if (!is_await && my keyword != VAR_INIT_USING) return;
    if (vec_initialized_binding_is_empty(&my initializers)) return; // The parser reported it
    if (for_in) {
        if (is_await) {
            error_ctx_grammar_error_d(errs, my span, 1494,
                "The left-hand side of a 'for...in' statement cannot be an 'await using' declaration.");
        } else {
            error_ctx_grammar_error_d(errs, my span, 1493,
                "The left-hand side of a 'for...in' statement cannot be a 'using' declaration.");
        }
        return;
    }
    if (is_await && my in_static_block) {
        error_ctx_error_d(errs, my span, 18054, // Regardless of parse errors, like tsc
            "'await using' statements cannot be used inside a class static block.");
    } else if (is_await) {
        // (In a function the parser reported it)
        await_check_grammar(my at_top_level ? AWAIT_AT_TOP_LEVEL : AWAIT_ALLOWED, AWAIT_KIND_USING, my await_span, nullptr, errs);
    }
    for_each (i, vec_initialized_binding, &my initializers) {
        if (i->binding.tag == BINDING_IDENTIFIER) continue;
        error_ctx_grammar_error_d(errs, i->binding.content_span, 1492,
            "'", variable_statement_keyword_str(me), "' declarations may not have binding patterns.");
    }
}

// tsc's checkGrammarNameInLetOrConstDeclarations(): `let` can't be a name a let/const/using declaration binds
static void binding_check_let_name(Binding const* me, ErrorCtx* errs) {
    switch (my tag) {
        case BINDING_NOOP: break;
        case BINDING_IDENTIFIER:
            if (csview_equals(my content_span.sv, "let")) {
                error_ctx_grammar_error_d(errs, my content_span, 2480,
                    "'let' is not allowed to be used as a name in 'let' or 'const' declarations.");
            }
            break;
        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) binding_check_let_name(i, errs);
            break;
        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) binding_check_let_name(&i->target, errs);
            break;
    }
}

// The last part of tsc's checkGrammarVariableDeclaration(), which the earlier grammar errors of the declaration skip
static void variable_statement_check_let_names(VariableStatement const* me, InitializedBinding const* i, ErrorCtx* errs) {
    if (my keyword == VAR_INIT_VAR) return;
    // `using` patterns got TS1492
    if ((my keyword == VAR_INIT_USING || my keyword == VAR_INIT_AWAIT_USING) && i->binding.tag != BINDING_IDENTIFIER) return;
    binding_check_let_name(&i->binding, errs);
}

// `/** @enum {T} */ const E = { a: v }`: every member's value is checked against T (tsc's checkObjectLiteral)
static void variable_check_jsdoc_enum(InitializedBinding const* me, ErrorCtx* errs) {
    if (my binding.tag != BINDING_IDENTIFIER || !my binding.symbol || my value.tag != EXPR_OBJECT) return;
    SourceFile const* file = my binding.content_span.source;
    if (!file || !file->is_js) return;
    Symbol const* symbol = my binding.symbol;
    TypeDecl const* enum_decl = nullptr;
    for (u32 d = 0; d < symbol->decl_count && !enum_decl; d++) {
        TypeDecl const* decl = symbol->decls[d].kind == SYMBOL_DECL_TYPE_ALIAS ? symbol->decls[d].node : nullptr;
        if (decl && decl->jsdoc_enum) enum_decl = decl;
    }
    if (!enum_decl) return;
    Ty const* type = ty_of_node(&enum_decl->type);
    for_each (i, vec_object_expr_item, &my value.object) {
        if (i->tag != OBJECT_ITEM_PROPERTY || i->jsdoc_type) continue;
        ty_check_assignable_expr(current_checker, expr_get_type(&i->value), type, i->span, &i->value, errs);
    }
}

// tsc's checkESModuleMarker: the first name a binding declares
static void binding_check_es_module_marker(Binding const* me, ErrorCtx* errs) {
    switch (my tag) {
        case BINDING_IDENTIFIER:
            if (csview_equals(my content_span.sv, "__esModule")) {
                error_ctx_grammar_error_d(errs, my content_span, 1216,
                    "Identifier expected. '__esModule' is reserved as an exported marker when transforming ECMAScript modules.");
            }
            break;
        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) {
                if (i->tag == BINDING_NOOP) continue;
                binding_check_es_module_marker(i, errs);
                return;
            }
            break;
        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                binding_check_es_module_marker(&i->target, errs);
                return;
            }
            break;
        default:
            break;
    }
}

void variable_statement_typecheck(VariableStatement* me, bool declared, bool exported, ErrorCtx* errs) {
    variable_statement_check_using(me, false, errs);
    // An export of a module emitted as CommonJS (or AMD, UMD) can't be named like the marker the emit adds
    bool check_marker = exported && !declared && !compiler_options()->no_emit && !vec_initialized_binding_is_empty(&my initializers) &&
        source_file_emit_module_kind(my initializers.data[0].binding.content_span.source) < MODULE_KIND_SYSTEM;
    for_each (i, vec_initialized_binding, &my initializers) {
        if (check_marker) binding_check_es_module_marker(&i->binding, errs);
        // An ambient variable without a type has nothing for control flow to type it by
        if (declared && compiler_options()->no_implicit_any && i->binding.tag == BINDING_IDENTIFIER &&
            !i->binding.has_explicit_type && i->value.tag == EXPR_IMPLICIT_ANY
        ) {
            error_ctx_error_d(errs, i->binding.content_span, 7005, "Variable '", i->binding.content_span,
                "' implicitly has an 'any' type.");
        }
        if (variable_statement_is_const(me) &&
            i->value.tag == EXPR_IMPLICIT_ANY &&
            !declared
        ) {
            error_ctx_grammar_error_d(errs, i->binding.content_span, 1155,
                "'", variable_statement_keyword_str(me), "' declarations must be initialized.");
        } else if (declared &&
            i->value.tag != EXPR_IMPLICIT_ANY
        ) {
            if (i->binding.has_explicit_type || !variable_statement_is_const(me)) {
                error_ctx_error_d(errs, i->value.outer_span, 1039,
                    "Initializers are not allowed in ambient contexts.");
            } else {
                bool valid = expr_is_ambient_const_initializer(&i->value);
                if (!valid) {
                    error_ctx_error_d(errs, i->value.outer_span, 1254,
                        "A 'const' initializer in an ambient context must be a string or numeric literal or literal enum reference.");
                }
                if (valid) variable_statement_check_let_names(me, i, errs);
            }
        } else {
            variable_statement_check_let_names(me, i, errs);
        }
        initialized_binding_typecheck(i, errs);
        variable_check_jsdoc_enum(i, errs);
        if ((my keyword == VAR_INIT_USING || my keyword == VAR_INIT_AWAIT_USING) && i->value.tag != EXPR_IMPLICIT_ANY) {
            // tsc widens it first: no excess property checks
            Ty const* type = ty_widened_objects(current_checker, expr_get_type(&i->value));
            ty_check_using_initializer(current_checker, type, my keyword == VAR_INIT_AWAIT_USING,
                i->value.outer_span, errs);
        }
    }
}

// tsc's checkAmbientInitializer: what a const or readonly declaration without a type may be initialized with where
// nothing is emitted. TODO: an identifier or property access has to be a literal enum member
bool expr_is_ambient_const_initializer(Expr const* me) {
    switch (my tag) {
        case EXPR_LITERAL:
            return my literal.tag == VALUE_NUMBER || my literal.tag == VALUE_STRING || my literal.tag == VALUE_BOOLEAN ||
                my literal.tag == VALUE_BIGINT;
        case EXPR_UNARY_OP: {
            // `-1`, `-1n` (isStringOrNumberLiteralExpression, isBigIntLiteralExpression)
            Expr const* operand = &my unary_op->expr;
            return my unary_op->op == UNARY_OP_MINUS && operand->tag == EXPR_LITERAL &&
                (operand->literal.tag == VALUE_NUMBER || operand->literal.tag == VALUE_BIGINT);
        }
        case EXPR_IDENTIFIER:
        case EXPR_PROPERTY:
            return true;
        default:
            return false;
    }
}

// Type declaration

void type_decl_drop(TypeDecl* me) {
    generic_drop(&my generic);
    type_drop(&my type);
}

void type_decl_add_names(TypeDecl* me, Scope* scope, DeclStatementProps props) {
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = my jsdoc && !my jsdoc_enum && scope->kind == SCOPE_MODULE ? DECL_EXPORTED_YES : props.exported,
        .kind = SYMBOL_DECL_TYPE_ALIAS,
        .node = me,
    });
}

void type_decl_typecheck(TypeDecl* me, ErrorCtx* errs) {
    // A literal checked for its index signatures is the alias's type: named by the alias, which has to come first
    if (my symbol && type_literal_has_index_constraints(&my type)) ty_declared_type_of(current_checker, my symbol);
    generic_typecheck(&my generic, errs);
    ty_check_variance_annotations(current_checker, my symbol, &my generic, true, errs);
    // tsc's checkTypeReferenceNode resolves every reference it walks. When the reference is the body, that is the alias's
    // own type, which lowers it in the alias's context (a deferred reference, the alias name)
    if (my symbol && my type.tag == TYPE_IDENTIFIER) {
        CurrentNode saved = ty_enter_node(current_checker, identifier_type_span(my type.identifier));
        ty_declared_type_of(current_checker, my symbol);
        ty_leave_node(current_checker, saved);
    }
    type_typecheck(&my type, errs);
}

// Namespace declaration

void namespace_decl_drop(NamespaceDecl* me) {
    block_drop(&my contents);
}

static bool block_is_instantiated(Block const* me, bool const_enums, Scope const* outer, u32 depth);

typedef enum InstanceState { NON_INSTANTIATED, CONST_ENUM_ONLY, INSTANTIATED } InstanceState;

// The instance state of a statement that declares `name`, NON_INSTANTIATED if it declares nothing of that name (tsc's
// nodeHasName and getModuleInstanceStateCached). found: whether it declares it
static InstanceState statement_instance_state_of(Statement const* me, csview name, Scope const* outer, u32 depth, bool* found) {
    *found = false;
    switch (my tag) {
        case STATEMENT_DECL_TYPE:
            *found = csview_equals_sv(my decl.type.name.sv, name);
            return NON_INSTANTIATED;
        case STATEMENT_DECL_INTERFACE:
            *found = csview_equals_sv(my decl.interface->name.sv, name);
            return NON_INSTANTIATED;
        case STATEMENT_DECL_ENUM:
            *found = csview_equals_sv(my decl.enum_->name.sv, name);
            return my decl.enum_->is_const ? CONST_ENUM_ONLY : INSTANTIATED;
        case STATEMENT_DECL_FUNCTION:
            *found = csview_equals_sv(my decl.function.impl->name.sv, name);
            return INSTANTIATED;
        case STATEMENT_DECL_CLASS:
            *found = csview_equals_sv(my decl.class->name.sv, name);
            return INSTANTIATED;
        case STATEMENT_DECL_VARIABLE:
            for_each (i, vec_initialized_binding, &my decl.variable.initializers) {
                if (i->binding.tag == BINDING_IDENTIFIER && csview_equals_sv(i->binding.content_span.sv, name)) *found = true;
            }
            return INSTANTIATED;
        case STATEMENT_DECL_NAMESPACE:
            *found = !my decl.namespace.is_ambient_module && csview_equals_sv(my decl.namespace.name.sv, name);
            if (!*found) return NON_INSTANTIATED;
            return block_is_instantiated(&my decl.namespace.contents, true, outer, depth + 1) ? INSTANTIATED : NON_INSTANTIATED;
        case STATEMENT_IMPORT_EXPORT: {
            // A re-export of `import x = N.x` is instantiated, it's ambiguous. Other imports aren't
            PortStatement const* port = my import_export;
            bool is_export = span_equals(port->keyword_span, "export");
            if (!is_export && csview_equals_sv(port_item_name(port->main.decl_name), name)) *found = true;
            for_each (i, vec_port_item, &port->items) {
                if (!is_export && csview_equals_sv(port_item_name(i->decl_name), name)) *found = true;
            }
            return *found && port->main.tag == PORT_IMPORT_ALIAS ? INSTANTIATED : NON_INSTANTIATED;
        }
        default:
            return NON_INSTANTIATED;
    }
}

// tsc's getModuleInstanceStateForAliasTarget: `export { a }` is as instantiated as the declarations of `a` in the
// nearest block that has any (`me`, then the blocks of the scopes around it). Instantiated if there are none
static InstanceState alias_target_state(Block const* me, csview name, Scope const* outer, u32 depth) {
    if (depth > 16) return NON_INSTANTIATED; // (A cycle: tsc's visited map says non-instantiated)
    Block const* block = me;
    Scope const* scope = outer;
    while (block) {
        bool any = false;
        InstanceState state = NON_INSTANTIATED;
        for_each (i, vec_statement, &block->statements) {
            bool found;
            InstanceState s = statement_instance_state_of(i, name, scope, depth, &found);
            if (!found) continue;
            any = true;
            if (s > state) state = s;
        }
        if (any) return state;
        while (scope && (!scope->block || scope->block == block)) scope = scope->parent;
        block = scope ? scope->block : nullptr;
    }
    return INSTANTIATED;
}

// tsc's getModuleInstanceState(), syntactically: a namespace is a value if it contains more than types
// const_enums: whether a `const enum` counts (it does for the symbol, for unreachable code only when it is emitted).
// outer: the scope the namespace is declared in, where the names it exports without declaring are looked up
static bool block_is_instantiated(Block const* me, bool const_enums, Scope const* outer, u32 depth) {
    for_each (i, vec_statement, &my statements) {
        switch (i->tag) {
            case STATEMENT_NOOP:
                // `;` and `debugger` are statements like any other to tsc's getModuleInstanceState, what the parser
                // made of an error isn't there
                if (span_valid(i->span) && i->span.sv.size && (i->span.sv.buf[0] == ';' || i->span.sv.buf[0] == 'd')) return true;
                break;
            case STATEMENT_DECL_TYPE:
            case STATEMENT_DECL_INTERFACE:
                break;

            case STATEMENT_DECL_ENUM:
                if (const_enums || !i->decl.enum_->is_const) return true;
                break;

            case STATEMENT_IMPORT_EXPORT: {
                PortStatement const* port = i->import_export;
                bool is_export = span_equals(port->keyword_span, "export");
                if (is_export && !port->module_ref && port->main.tag == PORT_NOOP && !port->main.export_import) {
                    // `export { a, b }`
                    for_each (item, vec_port_item, &port->items) {
                        Span source = span_valid(item->source_name) ? item->source_name : item->decl_name;
                        InstanceState state = alias_target_state(me, port_item_name(source), outer, depth);
                        if (state == INSTANTIATED || (state == CONST_ENUM_ONLY && const_enums)) return true;
                    }
                    break;
                }
                if (port_item_is_export(&port->main) || port->main.export_import || is_export) return true;
                break;
            }

            case STATEMENT_DECL_NAMESPACE:
                // (A `declare global` too, by its contents: tsc's getModuleInstanceState)
                if (block_is_instantiated(&i->decl.namespace.contents, const_enums, outer, depth + 1)) {
                    return true;
                }
                break;

            default:
                return true;
        }
    }
    return false;
}

void namespace_decl_add_names(NamespaceDecl* me, Scope* scope, DeclStatementProps props) {
    // tsc's checkGrammarModuleElementContext: directly in a file or a namespace body, else only this is said about it
    bool misplaced = my labeled || !(scope->kind == SCOPE_SCRIPT || scope->kind == SCOPE_MODULE ||
        scope->kind == SCOPE_NAMESPACE || scope->kind == SCOPE_GLOBAL_AUGMENTATION);
    ErrorCtx* errs = misplaced ? nullptr : scope->errs;
    bool checked = true;
    for (Scope const* i = scope; i && checked; i = i->parent) checked = !i->with_body;
    if (misplaced && checked) {
        Span first = my full_span;
        isize len = 0;
        while (len < first.sv.size && (isalnum((unsigned char)first.sv.buf[len]) || first.sv.buf[len] == '_')) len++;
        first.sv.size = len;
        error_ctx_grammar_error_d(scope->errs, first, (my is_global || my is_ambient_module) ? 1234 : 1235,
            (my is_global || my is_ambient_module) ? "An ambient module declaration is only allowed at the top level in a file." :
            "A namespace declaration is only allowed at the top level of a namespace or module.");
    }
    if (my is_global && !props.declared && !scope->ambient) {
        error_ctx_error_d(errs, my name, 2670,
            "Augmentations for the global scope should have 'declare' modifier unless they appear in already ambient context.");
    }

    // tsc's checkModuleDeclaration: where `declare global` and `declare module "m"` can be. Augmentations
    // (isModuleAugmentationExternal) are at the top level of a module or directly in an ambient module of a script,
    // an ambient module can also be at the top level of a script, not a global augmentation
    bool invalid_global = false;
    if (my is_global || my is_ambient_module) {
        bool in_script_ambient_module = scope->kind == SCOPE_NAMESPACE && scope->container &&
            scope->container->is_module && scope->parent && scope->parent->kind == SCOPE_SCRIPT;
        if (my is_global && (scope->kind == SCOPE_SCRIPT || (scope->kind != SCOPE_MODULE && !in_script_ambient_module))) {
            error_ctx_error_d(errs, my name, 2669, "Augmentations for the global scope can only be directly nested "
                "in external modules or ambient module declarations.");
            // It augments nothing then: a namespace named `global` (tsc's binder declares a ValueModule for it)
            my is_global = false;
            my is_invalid_global = invalid_global = true;
        } else if (!my is_global && scope->kind == SCOPE_SCRIPT && my module_ref &&
            module_name_is_relative(cstr_sv(&my module_ref->path))
        ) {
            error_ctx_error_d(errs, my name, 2436, "Ambient module declaration cannot specify relative module name.");
        } else if (!my is_global && scope->kind != SCOPE_SCRIPT && scope->kind != SCOPE_MODULE && !in_script_ambient_module) {
            error_ctx_error_d(errs, my name, 2435, "Ambient modules cannot be nested in other modules or namespaces.");
        }
    }

    if (my is_global) {
        my inner_scope = scope_new(scope, SCOPE_GLOBAL_AUGMENTATION);
    } else {
        my inner_scope = scope_new(scope, SCOPE_NAMESPACE);
        if (my is_ambient_module) {
            // In a script it declares a module, and every block of that name goes into the one module symbol. In a
            // module it augments the module it names: the block declares into a symbol of its own, which
            // binder_link() merges into that module once aliases can be resolved (an augmented name may be an alias
            // of the module, the declarations then go to its target)
            Symbol* module = nullptr;
            bool augmentation = scope->kind == SCOPE_MODULE;
            if (augmentation && !my is_shorthand) {
                module = binder_new_symbol(scope->binder, my name.sv, SYMBOL_VALUE_MODULE);
                module->name_span = my name;
                module->exports = arena_new(&scope->binder->arena, SymbolTable);
                binder_add_augmentation(scope->binder, me);
            }
            if (!module) {
                module = binder_ambient_module(scope->binder, cstr_sv(&my module_ref->path), my name);
                binder_add_module_declaration(module, me);
            }
            if (my is_shorthand) module->is_shorthand_module = true;
            my symbol = module;
            my inner_scope->container = module;
            my is_augmentation = augmentation;
        } else {
            bool has_value = invalid_global || block_is_instantiated(&my contents, true, scope, 0);
            scope_init_declaration(scope, &my symbol, (DeclProps){
                .name = my name,
                .full = my full_span,
                .exported = props.exported,
                .kind = SYMBOL_DECL_NAMESPACE,
                .node = me,
                .is_instantiated = has_value,
                .is_ambient = props.declared,
            });
            my inner_scope->container = my symbol;
        }
    }
    my inner_scope->ambient = scope->ambient || props.declared;
    my inner_scope->export_context = my inner_scope->ambient && !block_has_export_declarations(&my contents);
    if (my is_ambient_module && my is_augmentation) my inner_scope->export_context = true;

    block_add_names(&my contents, my inner_scope);
    scope_check_export_merges(my inner_scope, block_span(&my contents));
}

void namespace_decl_resolve_names(NamespaceDecl* me, Scope* names, ErrorCtx* errs) {
    // The scope was made when the namespace was declared
    my inner_scope->flow = names->flow;
    my inner_scope->self = my symbol;
    block_lookup_names_container(&my contents, false, c_sv("namespace"), my name, my inner_scope, errs);
    my inner_scope->flow = nullptr;
}

// tsc's getFirstNonAmbientClassOrFunctionDeclaration
static SymbolDecl const* first_non_ambient_class_or_function(Symbol const* symbol) {
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->kind == SYMBOL_DECL_CLASS && !((ClassDecl const*)decl->node)->is_ambient) return decl;
        if (decl->kind == SYMBOL_DECL_FUNCTION) {
            FunctionImpl const* function = decl->node;
            if (function->has_body && !function->ambient) return decl;
        }
    }
    return nullptr;
}

void namespace_decl_typecheck(NamespaceDecl* me, bool declared, ErrorCtx* errs) {
    // tsc's checkModuleDeclaration: an instantiated namespace merged with a class or function comes after it, in the
    // same file
    if (!declared && !my is_global && !my is_ambient_module && my symbol && my symbol->decl_count > 1 &&
        namespace_decl_is_instantiated(me)
    ) {
        SymbolDecl const* first = first_non_ambient_class_or_function(my symbol);
        if (first && first->full.source != my full_span.source) {
            error_ctx_error_d(errs, my name, 2433,
                "A namespace declaration cannot be in a different file from a class or function with which it is merged.");
        } else if (first && span_start_ptr(my full_span) < span_start_ptr(first->full)) {
            error_ctx_error_d(errs, my name, 2434,
                "A namespace declaration cannot be located prior to a class or function with which it is merged.");
        }
    }
    block_typecheck(&my contents, (BlockTypecheckCtx){ .ambient = declared }, errs);
}

// If statements

zsview for_each_loop_statement_keyword_str(ForEachLoopStatement const* me) {
    switch (my keyword) {
        case FOR_EACH_LOOP_OF: return c_zv("of");
        case FOR_EACH_LOOP_IN: return c_zv("in");
    }
    impossible;
}

Ty const* for_each_loop_statement_evaluate_var_type(ForEachLoopStatement* me) {
    Checker* checker = current_checker;
    Ty const* iterable = ty_expr_type(checker, &my iterable);
    if (iterable == ty_intrinsic(checker, TY_CIRCULAR)) return iterable; // Self-referential
    switch (my keyword) {
        case FOR_EACH_LOOP_IN:
            return ty_for_in_key_type(checker, iterable);

        case FOR_EACH_LOOP_OF:
            return ty_widened_nullable(checker, ty_iterated_type(checker, my is_await ? ITERATION_FOR_AWAIT_OF : ITERATION_FOR_OF, iterable,
                my iterable.outer_span, nullptr));
    }
    impossible;
}

Ty const* for_each_loop_statement_pattern_source_type(ForEachLoopStatement const* me) {
    Checker* checker = current_checker;
    Ty const* var_type = lazy_type_get((LazyType*)&my var_type);
    if (my keyword != FOR_EACH_LOOP_OF || var_type == ty_intrinsic(checker, TY_CIRCULAR)) return var_type;
    return ty_iterated_type(checker, my is_await ? ITERATION_FOR_AWAIT_OF : ITERATION_FOR_OF,
        ty_expr_type(checker, (Expr*)&my iterable), my iterable.outer_span, nullptr);
}

// What `for (k in x)` takes: object types and type parameters (every member of a union, some member of an
// intersection: `T & object` is what `typeof t === "object"` makes of a T). Without strict null checks also null
static bool for_in_accepts(Checker* checker, Ty const* ty) {
    if (ty->flags & (TYF_ANY | TYF_OBJECT | TYF_NON_PRIMITIVE | TYF_TYPE_PARAM | TYF_DEFERRED)) return true;
    if (ty->flags & TYF_NULLABLE) return !compiler_options()->strict_null_checks;
    if (ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!for_in_accepts(checker, ty->composite.types.data[i])) return false;
        }
        return ty->composite.types.len > 0;
    }
    if (ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (for_in_accepts(checker, ty->composite.types.data[i])) return true;
        }
    }
    return false;
}

// Statements

void statement_drop(Statement* me) {
    switch (my tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_SOURCE_DIRECTIVE:
            cstr_drop(&my source_directive->path);
            cstr_drop(&my source_directive->not_found);
            free(my source_directive);
            break;

        case STATEMENT_BLOCK:
            block_drop(&my block);
            break;

        case STATEMENT_IF_ELSE:
            vec_expr_drop(&my if_else.conditions);
            vec_statement_drop(&my if_else.statements);
            break;

        case STATEMENT_FOR_LOOP:
            statement_drop(&my for_loop->initialization);
            expr_drop(&my for_loop->condition);
            expr_drop(&my for_loop->afterthought);
            statement_drop(&my for_loop->body);
            free(my for_loop);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT: variable_statement_drop(&my for_each_loop->variable.statement); break;
                case FOR_EACH_LOOP_EXPR: expr_drop(&my for_each_loop->variable.expr); break;
            }
            lazy_type_drop(&my for_each_loop->var_type);
            expr_drop(&my for_each_loop->iterable);
            statement_drop(&my for_each_loop->body);
            free(my for_each_loop);
            break;

        case STATEMENT_SWITCH:
            expr_drop(&my switch_->needle);
            vec_switch_case_drop(&my switch_->cases);
            free(my switch_);
            break;

        case STATEMENT_TRY_CATCH:
            block_drop(&my try_catch->try_block);
            binding_drop(&my try_catch->catch_var);
            block_drop(&my try_catch->catch_block);
            block_drop(&my try_catch->finally_block);
            free(my try_catch);
            break;

        case STATEMENT_LABELED:
            statement_drop(my labeled.body);
            free(my labeled.body);
            break;

        case STATEMENT_WITH:
            expr_drop(&my with->object);
            statement_drop(&my with->body);
            free(my with);
            break;

        case STATEMENT_BREAK: break;
        case STATEMENT_CONTINUE: break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            expr_drop(&my expr);
            if (my expr_jsdoc_type) {
                type_drop(my expr_jsdoc_type);
                free(my expr_jsdoc_type);
            }
            break;

        case STATEMENT_IMPORT_EXPORT:
            port_statement_drop(my import_export);
            free(my import_export);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_drop(&my decl.variable);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_drop(my decl.function.impl);
            free(my decl.function.impl);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_drop(my decl.enum_);
            free(my decl.enum_);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_drop(my decl.class);
            free(my decl.class);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_drop(&my decl.type);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_drop(my decl.interface);
            free(my decl.interface);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_drop(&my decl.namespace);
            break;
    }
}

// The variables of `for (var/let x of y)`: their type comes from the iterable
static void for_each_loop_add_names(ForEachLoopStatement* me, Scope* scope) {
    if (my variable.tag != FOR_EACH_LOOP_STATEMENT) return;
    VecInitializedBinding* initializers = &my variable.statement.initializers;
    intptr_t var_size = vec_initialized_binding_size(initializers);
    if (var_size > 1) {
        // (tsc's grammarErrorOnFirstToken: a pattern's `{` or `[`)
        Binding const* second = &vec_initialized_binding_at(initializers, 1)->binding;
        Span span = second->content_span;
        if (second->tag == BINDING_ARRAY || second->tag == BINDING_OBJECT) {
            span = span_make(span.source, span_start_ptr(span), span_start_ptr(span) + 1);
        }
        error_ctx_error_d(
            scope->errs,
            span,
            my keyword == FOR_EACH_LOOP_OF ? 1188 : 1091,
            "Only a single variable declaration is allowed in a 'for...",
            for_each_loop_statement_keyword_str(me).str, "' statement."
        );
    } else if (var_size == 0) {
        // Error added by parser
    } else {
        InitializedBinding* initializer = initializers->data;
        if (initializer->value.tag != EXPR_IMPLICIT_ANY) {
            error_ctx_error_d(
                scope->errs,
                initializer->binding.content_span,
                1189,
                "The variable declaration of a 'for...",
                for_each_loop_statement_keyword_str(me).str,
                "' statement cannot have an initializer."
            );
        } else if (initializer->binding.has_explicit_type) {
            error_ctx_error_d(
                scope->errs,
                initializer->binding.content_span,
                2404,
                "The left-hand side of a 'for...",
                for_each_loop_statement_keyword_str(me).str,
                "' statement cannot use a type annotation."
            );
        }
    }
    for_each (i, vec_initialized_binding, &my variable.statement.initializers) {
        // If we set has_explicit_type = false here but don't clear the type, the type will still show
        // up in test AST traversial (as required), but will not be typechecked (as required). This
        // is dumb but it works.
        i->binding.has_explicit_type = false;
        binding_add_names(&i->binding, &my var_type, scope, (BindingAddNamesProps){
            .function_scoped = my variable.statement.keyword == VAR_INIT_VAR,
        });
    }
}

static void block_hoist_names(Block* me, Scope* scope) {
    for_each (i, vec_statement, &my statements) {
        statement_hoist_names(i, scope);
    }
}

// Declares the `var`s of nested statements in the function, module or namespace scope they belong to. Never looks
// into functions. The blocks they're in skip them when their turn comes (scope_init_declaration())
void statement_hoist_names(Statement* me, Scope* scope) {
    switch (my tag) {
        case STATEMENT_DECL_FUNCTION:
            // (`export function` in a block of a namespace is exported: tsc's declareSymbolAndAddToSymbolTable goes by the
            // container, declareModuleMember)
            if (!scope->strict) {
                function_impl_add_names(my decl.function.impl, scope,
                    my decl.props.exported == DECL_EXPORTED_YES ? DECL_EXPORTED_YES : DECL_EXPORTED_NO, true, my decl.props.declared);
            }
            break;

        case STATEMENT_BLOCK:
            block_hoist_names(&my block, scope);
            break;

        case STATEMENT_IF_ELSE:
            for_each (i, vec_statement, &my if_else.statements) {
                statement_hoist_names(i, scope);
            }
            break;

        case STATEMENT_FOR_LOOP:
            statement_hoist_names(&my for_loop->initialization, scope);
            statement_hoist_names(&my for_loop->body, scope);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            if (my for_each_loop->variable.tag == FOR_EACH_LOOP_STATEMENT &&
                my for_each_loop->variable.statement.keyword == VAR_INIT_VAR
            ) {
                for_each_loop_add_names(my for_each_loop, scope);
            }
            statement_hoist_names(&my for_each_loop->body, scope);
            break;

        case STATEMENT_SWITCH:
            for_each (i, vec_switch_case, &my switch_->cases) {
                for_each (j, vec_statement, &i->statements) {
                    statement_hoist_names(j, scope);
                }
            }
            break;

        case STATEMENT_TRY_CATCH:
            block_hoist_names(&my try_catch->try_block, scope);
            block_hoist_names(&my try_catch->catch_block, scope);
            block_hoist_names(&my try_catch->finally_block, scope);
            break;

        case STATEMENT_LABELED:
            statement_hoist_names(my labeled.body, scope);
            break;

        case STATEMENT_WITH:
            statement_hoist_names(&my with->body, scope);
            break;

        case STATEMENT_DECL_VARIABLE:
            if (my decl.variable.keyword == VAR_INIT_VAR) {
                variable_statement_add_names(&my decl.variable, scope, my decl.props.exported, my decl.props.declared);
            }
            break;

        default:
            break;
    }
}

static void block_collect_var_names(Block const* me, VecSpan* out) {
    for_each (i, vec_statement, &my statements) statement_collect_var_names(i, out);
}

static void variable_statement_collect_names(VariableStatement const* me, VecSpan* out) {
    for_each (i, vec_initialized_binding, &my initializers) binding_collect_names(&i->binding, out);
}

// The names of the `var`s statement_hoist_names() hoists, for strict code (no function declarations)
void statement_collect_var_names(Statement const* me, VecSpan* out) {
    switch (my tag) {
        case STATEMENT_BLOCK:
            block_collect_var_names(&my block, out);
            break;
        case STATEMENT_IF_ELSE:
            for_each (i, vec_statement, &my if_else.statements) statement_collect_var_names(i, out);
            break;
        case STATEMENT_FOR_LOOP:
            statement_collect_var_names(&my for_loop->initialization, out);
            statement_collect_var_names(&my for_loop->body, out);
            break;
        case STATEMENT_FOR_EACH_LOOP:
            if (my for_each_loop->variable.tag == FOR_EACH_LOOP_STATEMENT &&
                my for_each_loop->variable.statement.keyword == VAR_INIT_VAR
            ) {
                variable_statement_collect_names(&my for_each_loop->variable.statement, out);
            }
            statement_collect_var_names(&my for_each_loop->body, out);
            break;
        case STATEMENT_SWITCH:
            for_each (i, vec_switch_case, &my switch_->cases) {
                for_each (j, vec_statement, &i->statements) statement_collect_var_names(j, out);
            }
            break;
        case STATEMENT_TRY_CATCH:
            block_collect_var_names(&my try_catch->try_block, out);
            block_collect_var_names(&my try_catch->catch_block, out);
            block_collect_var_names(&my try_catch->finally_block, out);
            break;
        case STATEMENT_LABELED:
            statement_collect_var_names(my labeled.body, out);
            break;
        case STATEMENT_WITH:
            statement_collect_var_names(&my with->body, out);
            break;
        case STATEMENT_DECL_VARIABLE:
            if (my decl.variable.keyword == VAR_INIT_VAR) variable_statement_collect_names(&my decl.variable, out);
            break;
        default:
            break;
    }
}

void function_collect_local_value_names(FunctionImpl const* function, VecSpan* out) {
    for_each (i, vec_function_arg, &function->args) binding_collect_names(&i->initializer.binding, out);
    for_each (i, vec_statement, &function->body.statements) {
        switch (i->tag) {
            case STATEMENT_DECL_VARIABLE:
                variable_statement_collect_names(&i->decl.variable, out);
                break;
            case STATEMENT_DECL_FUNCTION:
                if (span_valid(i->decl.function.impl->name)) vec_span_push(out, i->decl.function.impl->name);
                break;
            case STATEMENT_DECL_CLASS:
                if (span_valid(i->decl.class->name)) vec_span_push(out, i->decl.class->name);
                break;
            case STATEMENT_DECL_ENUM:
                vec_span_push(out, i->decl.enum_->name);
                break;
            default:
                statement_collect_var_names(i, out);
                break;
        }
    }
}

void statement_add_names(Statement* me, Scope* scope) {
    switch (my tag) {
        case STATEMENT_EXPR:
            scope_add_commonjs_export(scope, &my expr, my span);
            break;

        case STATEMENT_EXPORT_DEFAULT_EXPR:
            scope_export_default_expr(scope, &my expr, my span, my export_equals);
            break;

        case STATEMENT_IMPORT_EXPORT:
            port_statement_add_names(my import_export, scope);
            break;

        case STATEMENT_DECL_VARIABLE:
            // The `var`s of nested blocks were declared by statement_hoist_names()
            if (my decl.variable.keyword == VAR_INIT_VAR && !scope_is_var_container(scope)) break;
            variable_statement_add_names(&my decl.variable, scope, my decl.props.exported, my decl.props.declared);
            break;

        case STATEMENT_DECL_FUNCTION:
            scope_check_strict_block_function(scope, my decl.function.impl->name);
            function_impl_add_names(my decl.function.impl, scope, my decl.props.exported,
                scope->kind == SCOPE_BLOCK && !scope->strict, my decl.props.declared);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_add_names(my decl.enum_, scope, my decl.props.exported, my decl.props.declared);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_add_names(my decl.class, scope, my decl.props.exported, my decl.props.declared);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_add_names(&my decl.type, scope, my decl.props);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_add_names(my decl.interface, scope, my decl.props.exported);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_add_names(&my decl.namespace, scope, my decl.props);
            break;

        case STATEMENT_LABELED:
            if (my labeled.body->tag == STATEMENT_DECL_NAMESPACE) my labeled.body->decl.namespace.labeled = true;
            statement_add_names(my labeled.body, scope);
            break;

        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
        case STATEMENT_BLOCK:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_WITH:
        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            break;
    }
}

bool namespace_decl_is_instantiated(NamespaceDecl const* me) {
    return block_is_instantiated(&my contents, compiler_options()->preserve_const_enums,
        my inner_scope ? my inner_scope->parent : nullptr, 0);
}

// tsc's getMeaningFromDeclaration: an ambient module or an instantiated namespace declares a value (const enums
// alone don't count, whatever preserveConstEnums says)
bool namespace_decl_declares_value(NamespaceDecl const* me) {
    return my is_ambient_module || my is_invalid_global || block_is_instantiated(&my contents, false, my inner_scope ? my inner_scope->parent : nullptr, 0);
}

// Control flow (flow.h). These follow the functions of the same names in tsc's binder

// Statements that can validly be used before they appear: not part of an unreachable range
static bool statement_is_executable(Statement const* me) {
    switch (my tag) {
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_TYPE:
        case STATEMENT_DECL_INTERFACE:
            return false;
        case STATEMENT_DECL_NAMESPACE:
            return namespace_decl_is_instantiated(&my decl.namespace);
        case STATEMENT_DECL_ENUM:
            return !my decl.enum_->is_const || compiler_options()->preserve_const_enums;
        case STATEMENT_DECL_VARIABLE:
            if (my decl.variable.keyword != VAR_INIT_VAR) return true;
            for_each (i, vec_initialized_binding, &my decl.variable.initializers) {
                if (i->value.tag == EXPR_IMPLICIT_ANY) return false; // `var x;` may declare a variable used above
            }
            return true;
        default:
            return true;
    }
}

// Whether the statement is unreachable. The first statement of unreachable code is where TS7027 goes, which is an
// error only when allowUnreachableCode is explicitly false (unset: a suggestion, which baselines don't have)
static bool statement_check_unreachable(Statement const* me, FlowBuilder* flow, VecStatement const* list) {
    if (!flow_is_unreachable(flow->current)) return false;
    if (flow->current != &flow_unreachable) return true;
    bool report;
    switch (my tag) {
        case STATEMENT_BLOCK: // The statements in it
        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_TYPE:
        case STATEMENT_DECL_INTERFACE:
        case STATEMENT_IMPORT_EXPORT:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
            report = false;
            break;
        case STATEMENT_DECL_ENUM:
        case STATEMENT_DECL_NAMESPACE:
            report = statement_is_executable(me);
            break;
        default:
            report = true;
            break;
    }
    if (!report) return true;
    flow->current = &flow_reported_unreachable;

    CompilerOptions const* options = compiler_options();
    bool is_error = options->is_set[COMPILER_OPTION_allow_unreachable_code] && !options->allow_unreachable_code &&
        !flow->ambient && statement_is_executable(me);
    if (!is_error) return true;
    if (!list || !statement_is_executable(me)) {
        error_ctx_error_d(flow->errs, my span, 7027, "Unreachable code detected.");
        return true;
    }
    // In a block: every run of executable statements from here on
    isize_t count = vec_statement_size(list);
    for (isize_t i = me - vec_statement_at(list, 0); i < count; i++) {
        if (!statement_is_executable(vec_statement_at(list, i))) continue;
        isize_t last = i;
        while (last + 1 < count && statement_is_executable(vec_statement_at(list, last + 1))) last++;
        error_ctx_error_d(
            flow->errs, span_between(vec_statement_at(list, i)->span, vec_statement_at(list, last)->span),
            7027, "Unreachable code detected."
        );
        i = last;
    }
    return true;
}

static void statement_resolve_names_body(Statement* me, Scope* names, ErrorCtx* errs, FlowNode* break_target, FlowNode* continue_target) {
    FlowBuilder* flow = names->flow;
    FlowNode* saved_break = flow->break_target;
    FlowNode* saved_continue = flow->continue_target;
    flow->break_target = break_target;
    flow->continue_target = continue_target;
    statement_resolve_names(me, names, errs);
    flow->break_target = saved_break;
    flow->continue_target = saved_continue;
}

// `a: b: while (x) {}`: `continue a` goes where the loop continues. Takes the labels the statement was given
static FlowNode* flow_set_continue_target(FlowBuilder* flow, u32 label_count, FlowNode* target) {
    FlowActiveLabel* label = flow->active_labels;
    for (u32 i = 0; i < label_count && label; i++, label = label->next) {
        label->continue_target = target;
    }
    return target;
}

static void if_else_resolve_names(IfStatement* me, isize_t index, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    FlowNode* then_label = flow_new_branch_label(flow);
    FlowNode* else_label = flow_new_branch_label(flow);
    FlowNode* post = flow_new_branch_label(flow);
    expr_lookup_names_condition(vec_expr_at_mut(&my conditions, index), names, errs, then_label, else_label);
    flow_set_current(flow, flow_finish_label(then_label));
    {
        Statement* branch = vec_statement_at_mut(&my statements, index);
        Scope* branch_scope = scope_new(names, SCOPE_BLOCK);
        statement_add_names(branch, branch_scope);
        statement_resolve_names(branch, branch_scope, errs);
    }
    flow_add_antecedent(flow, post, flow->current);
    flow_set_current(flow, flow_finish_label(else_label));
    if (index + 1 < vec_expr_size(&my conditions)) {
        // `else if`: an if statement of its own, which can be where unreachable code starts
        // TODO: TS7027 for it (there is no Statement to hand to statement_check_unreachable())
        if_else_resolve_names(me, index + 1, names, errs);
    } else if (index + 1 < vec_statement_size(&my statements)) {
        Statement* branch = vec_statement_at_mut(&my statements, index + 1);
        Scope* branch_scope = scope_new(names, SCOPE_BLOCK);
        statement_add_names(branch, branch_scope);
        statement_resolve_names(branch, branch_scope, errs);
    }
    flow_add_antecedent(flow, post, flow->current);
    flow_set_current(flow, flow_finish_label(post));
}

static void switch_resolve_names(SwitchStatement* me, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    FlowNode* post = flow_new_branch_label(flow);
    expr_lookup_names(&my needle, names, errs);
    FlowNode* saved_break = flow->break_target;
    FlowNode* saved_pre_switch = flow->pre_switch_case_flow;
    flow->break_target = post;
    flow->pre_switch_case_flow = flow->current;

    Scope* switch_scope = scope_new(names, SCOPE_BLOCK);
    for_each (i, vec_switch_case, &my cases) {
        for_each (j, vec_statement, &i->statements) {
            statement_add_names(j, switch_scope);
        }
    }
    bool is_narrowing = (my needle.tag == EXPR_LITERAL && my needle.literal.tag == VALUE_BOOLEAN && my needle.literal.boolean) ||
        expr_is_narrowing(&my needle);
    FlowNode* fallthrough = &flow_unreachable;
    bool has_default = false;
    isize_t count = vec_switch_case_size(&my cases);
    for (isize_t i = 0; i < count; i++) {
        isize_t clause_start = i;
        // Clauses without statements share the next one's
        while (!vec_statement_size(&vec_switch_case_at(&my cases, i)->statements) && i + 1 < count) {
            if (fallthrough == &flow_unreachable) flow_set_current(flow, flow->pre_switch_case_flow);
            SwitchCase* empty = vec_switch_case_at_mut(&my cases, i);
            if (empty->is_pattern) {
                FlowNode* saved = flow->current;
                flow_set_current(flow, flow->pre_switch_case_flow);
                expr_lookup_names(&empty->pattern, switch_scope, errs);
                flow_set_current(flow, saved);
            } else {
                has_default = true;
            }
            i++;
        }
        FlowNode* pre_case = flow_new_branch_label(flow);
        flow_add_antecedent(flow, pre_case, is_narrowing && !flow_is_unreachable(flow->pre_switch_case_flow) ?
            flow_new_switch_clause(flow, flow->pre_switch_case_flow, me, (u32)clause_start, (u32)i + 1) :
            flow->pre_switch_case_flow);
        flow_add_antecedent(flow, pre_case, fallthrough);
        flow_set_current(flow, flow_finish_label(pre_case));
        SwitchCase* clause = vec_switch_case_at_mut(&my cases, i);
        if (clause->is_pattern) {
            FlowNode* saved = flow->current;
            flow_set_current(flow, flow->pre_switch_case_flow);
            expr_lookup_names(&clause->pattern, switch_scope, errs);
            flow_set_current(flow, saved);
        } else {
            has_default = true;
        }
        for_each (j, vec_statement, &clause->statements) {
            statement_resolve_names(j, switch_scope, errs);
        }
        fallthrough = flow->current;
        if (compiler_options()->no_fallthrough_cases_in_switch && i != count - 1 && !flow_is_unreachable(flow->current)) {
            clause->fallthrough = flow->current;
        }
    }
    flow_add_antecedent(flow, post, flow->current);
    if (!has_default && !flow_is_unreachable(flow->pre_switch_case_flow)) {
        // "No clause matched": the checker decides whether that can happen
        flow_add_antecedent(flow, post, flow_new_switch_clause(flow, flow->pre_switch_case_flow, me, 0, 0));
    }
    flow->break_target = saved_break;
    flow->pre_switch_case_flow = saved_pre_switch;
    flow_set_current(flow, flow_finish_label(post));
}

static void try_catch_resolve_names(TryCatchStatement* me, bool has_catch, bool has_finally, Scope* names, ErrorCtx* errs) {
    // Any code in the try block can throw, but only assignments widen what a variable may be: they are the
    // antecedents of the exception label, which is where the catch block (or the finally block) starts
    FlowBuilder* flow = names->flow;
    FlowNode* saved_return = flow->return_target;
    FlowNode* saved_exception = flow->exception_target;
    FlowNode* normal_exit = flow_new_branch_label(flow);
    FlowNode* return_label = flow_new_branch_label(flow);
    FlowNode* exception_label = flow_new_branch_label(flow);
    if (has_finally) flow->return_target = return_label;
    flow_add_antecedent(flow, exception_label, flow->current);
    flow->exception_target = exception_label;
    block_resolve_names(&my try_block, names, errs);
    flow_add_antecedent(flow, normal_exit, flow->current);
    if (has_catch) {
        flow_set_current(flow, flow_finish_label(exception_label));
        // The catch block is like a second try block
        exception_label = flow_new_branch_label(flow);
        flow_add_antecedent(flow, exception_label, flow->current);
        flow->exception_target = exception_label;

        Scope* catch_scope = scope_new(names, SCOPE_BLOCK);
        binding_add_names(&my catch_var, nullptr, catch_scope, (BindingAddNamesProps){});
        binding_lookup_names(&my catch_var, catch_scope, errs);
        block_resolve_names(&my catch_block, catch_scope, errs);
        flow_add_antecedent(flow, normal_exit, flow->current);
    }
    flow->return_target = saved_return;
    flow->exception_target = saved_exception;
    if (!has_finally) {
        flow_set_current(flow, flow_finish_label(normal_exit));
        return;
    }
    // The finally block is reached from everywhere. Code after it only from the normal exits, a `return` target only
    // from the returns, an outer exception target only from the exceptions: that's what the reduce labels say
    FlowNode* finally_label = flow_new_branch_label(flow);
    finally_label->antecedents =
        flow_list_concat(flow, normal_exit->antecedents, exception_label->antecedents, return_label->antecedents);
    flow_set_current(flow, finally_label);
    block_resolve_names(&my finally_block, names, errs);
    if (flow->dead) return;
    if (flow_is_unreachable(flow->current)) {
        flow->current = &flow_unreachable;
        return;
    }
    if (flow->return_target && return_label->antecedents) {
        flow_add_antecedent(flow, flow->return_target,
            flow_new_reduce_label(flow, finally_label, return_label->antecedents, flow->current));
    }
    if (flow->exception_target && exception_label->antecedents) {
        flow_add_antecedent(flow, flow->exception_target,
            flow_new_reduce_label(flow, finally_label, exception_label->antecedents, flow->current));
    }
    flow->current = normal_exit->antecedents ?
        flow_new_reduce_label(flow, finally_label, normal_exit->antecedents, flow->current) : &flow_unreachable;
}

static void statement_resolve_names_labeled(Statement* me, u32 label_count, Scope* names, ErrorCtx* errs);

// label_count: how many labels the statement has
static void statement_resolve_names_reachable(Statement* me, u32 label_count, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    if (statement_declares_name(my tag) || my tag == STATEMENT_IMPORT_EXPORT) {
        unused_register(names, UNUSED_STATEMENT, me, span_null);
    }
    switch (my tag) {
        case STATEMENT_LABELED: {
            for (FlowActiveLabel* i = flow->active_labels; i; i = i->next) {
                if (!csview_equals_sv(i->name.sv, my labeled.name.sv)) continue;
                error_ctx_grammar_error_d(errs, my labeled.name, 1114, "Duplicate label '", my labeled.name.sv, "'.");
                break;
            }
            FlowNode* post = flow_new_branch_label(flow);
            FlowActiveLabel label = { .next = flow->active_labels, .name = my labeled.name, .break_target = post };
            flow->active_labels = &label;
            // A label on a label on a loop is that loop's label too
            statement_resolve_names_labeled(my labeled.body, label_count + 1, names, errs);
            CompilerOptions const* options = compiler_options();
            if (!label.referenced && !flow->dead && options->is_set[COMPILER_OPTION_allow_unused_labels] &&
                !options->allow_unused_labels
            ) {
                error_ctx_error_d(errs, my labeled.name, 7028, "Unused label.");
            }
            flow->active_labels = label.next;
            flow_add_antecedent(flow, post, flow->current);
            flow_set_current(flow, flow_finish_label(post));
        }   break;

        case STATEMENT_BLOCK:
            block_resolve_names(&my block, names, errs);
            break;

        case STATEMENT_IF_ELSE:
            if_else_resolve_names(&my if_else, 0, names, errs);
            break;

        case STATEMENT_WITH: {
            WithStatement* with = my with;
            Span keyword = span_make(my span.source, span_start_ptr(my span), span_start_ptr(my span) + 4);
            // A binder error in tsc (checkStrictModeWithStatement), reported in the body of another `with` too (errs is
            // null there, flow->errs is not). The rest are the checker's
            if (names->strict) {
                error_ctx_error_d(names->flow->errs, keyword, 1101, "'with' statements are not allowed in strict mode.");
            }
            // (tsc's checkWithStatement, which a `with` in the body of another never gets to: nothing in there is
            // reported, errs is null)
            if (names->function && names->function->is_async) {
                error_ctx_grammar_error_d(errs, keyword, 1300, "'with' statements are not allowed in an async function block.");
            }
            expr_lookup_names(&with->object, names, errs);
            // tsc binds the body like any other statement but never checks it, so nothing the checker would say
            // about it is said (not even names that aren't found)
            Scope* body_scope = scope_new(names, SCOPE_BLOCK);
            body_scope->with_body = true;
            statement_add_names(&with->body, body_scope);
            statement_resolve_names(&with->body, body_scope, nullptr);
        }   break;

        case STATEMENT_FOR_LOOP: {
            ForLoopStatement* loop = my for_loop;
            Scope* loop_scope = scope_new(names, SCOPE_BLOCK);
            statement_add_names(&loop->initialization, loop_scope);
            statement_add_names(&loop->body, loop_scope);
            if (loop->is_do_while) {
                // (Nothing in these, but every expression wants its names looked up)
                statement_resolve_names(&loop->initialization, loop_scope, errs);
                expr_lookup_names(&loop->afterthought, loop_scope, errs);
                FlowNode* pre_do = flow_new_loop_label(flow);
                FlowNode* pre_condition = flow_set_continue_target(flow, label_count, flow_new_branch_label(flow));
                FlowNode* post = flow_new_branch_label(flow);
                flow_add_antecedent(flow, pre_do, flow->current);
                flow_set_current(flow, pre_do);
                statement_resolve_names_body(&loop->body, loop_scope, errs, post, pre_condition);
                flow_add_antecedent(flow, pre_condition, flow->current);
                flow_set_current(flow, flow_finish_label(pre_condition));
                expr_lookup_names_condition(&loop->condition, loop_scope, errs, pre_do, post);
                flow_set_current(flow, flow_finish_label(post));
                break;
            }
            // `while (c)` is `for (; c; )`
            FlowNode* pre_loop = flow_set_continue_target(flow, label_count, flow_new_loop_label(flow));
            FlowNode* pre_body = flow_new_branch_label(flow);
            FlowNode* pre_incrementor = flow_new_branch_label(flow);
            FlowNode* post = flow_new_branch_label(flow);
            statement_resolve_names(&loop->initialization, loop_scope, errs);
            flow_add_antecedent(flow, pre_loop, flow->current);
            flow_set_current(flow, pre_loop);
            expr_lookup_names_condition(&loop->condition, loop_scope, errs, pre_body, post);
            flow_set_current(flow, flow_finish_label(pre_body));
            statement_resolve_names_body(&loop->body, loop_scope, errs, post, pre_incrementor);
            flow_add_antecedent(flow, pre_incrementor, flow->current);
            flow_set_current(flow, flow_finish_label(pre_incrementor));
            expr_lookup_names(&loop->afterthought, loop_scope, errs);
            flow_add_antecedent(flow, pre_loop, flow->current);
            flow_set_current(flow, flow_finish_label(post));
        }   break;

        case STATEMENT_FOR_EACH_LOOP: {
            ForEachLoopStatement* loop = my for_each_loop;
            await_check_grammar(loop->await_place, AWAIT_KIND_FOR, loop->await_span, names->function, errs);
            lazy_type_bind(&loop->var_type, LAZY_TYPE_FOR_EACH_LOOP_VAR);
            Scope* loop_scope = scope_new(names, SCOPE_BLOCK);
            bool is_declaration = loop->variable.tag == FOR_EACH_LOOP_STATEMENT;
            if (is_declaration && loop->variable.statement.keyword != VAR_INIT_VAR) {
                for_each_loop_add_names(loop, loop_scope);
            }
            FlowNode* pre_loop = flow_set_continue_target(flow, label_count, flow_new_loop_label(flow));
            FlowNode* post = flow_new_branch_label(flow);
            InitializedBinding const* outer_initializing = flow->initializing;
            if (is_declaration && vec_initialized_binding_size(&loop->variable.statement.initializers)) {
                flow->initializing = vec_initialized_binding_front(&loop->variable.statement.initializers);
            }
            expr_lookup_names(&loop->iterable, loop_scope, errs);
            flow->initializing = outer_initializing;
            flow_add_antecedent(flow, pre_loop, flow->current);
            flow_set_current(flow, pre_loop);
            flow_add_antecedent(flow, post, flow->current);
            if (is_declaration) {
                unused_register(loop_scope, UNUSED_FOR_IN_OF_VARIABLES, &loop->variable.statement, loop->variable.statement.span);
                for_each (i, vec_initialized_binding, &loop->variable.statement.initializers) {
                    // (What its pattern's computed names and defaults may not use yet)
                    flow->initializing = i;
                    binding_lookup_names(&i->binding, loop_scope, errs);
                    flow->initializing = outer_initializing;
                    expr_lookup_names(&i->value, loop_scope, errs);
                    if (loop->keyword == FOR_EACH_LOOP_IN && i->binding.tag == BINDING_IDENTIFIER) {
                        // (The loop too: `for (const k in ref)` acts as a non-null on ref, see type_at_flow_assignment())
                        flow_assign(loop_scope->flow, (FlowAssign){ .kind = FLOW_ASSIGN_DECLARATION, .binding = &i->binding, .for_each = loop });
                    } else {
                        binding_flow_assign(&i->binding, loop_scope);
                    }
                }
            } else {
                flow->assignment_target = FLOW_TARGET_DEFINITE;
                expr_lookup_names(&loop->variable.expr, names, errs);
                expr_flow_assign_for_each(&loop->variable.expr, names, loop);
            }
            statement_add_names(&loop->body, loop_scope);
            statement_resolve_names_body(&loop->body, loop_scope, errs, post, pre_loop);
            flow_add_antecedent(flow, pre_loop, flow->current);
            flow_set_current(flow, flow_finish_label(post));
        }   break;

        case STATEMENT_SWITCH:
            switch_resolve_names(my switch_, names, errs);
            break;

        case STATEMENT_TRY_CATCH:
            try_catch_resolve_names(my try_catch, my try_catch->has_catch, my try_catch->has_finally, names, errs);
            break;

        case STATEMENT_EXPR:
            expr_lookup_names_statement(&my expr, names, errs);
            if (my expr_jsdoc_type) {
                type_lookup_names(my expr_jsdoc_type, names, errs);
                scope_add_this_declaration(names, &my expr, my expr_jsdoc_type);
            }
            break;

        case STATEMENT_EXPORT_DEFAULT_EXPR: {
            // `export = x` doesn't use x before its declaration: the emit moves it (tsc)
            bool saved_type_query = flow->in_type_query;
            if (my export_equals && my expr.tag == EXPR_IDENTIFIER) flow->in_type_query = true;
            flow->type_only_alias_ok = my expr.tag == EXPR_IDENTIFIER;
            expr_lookup_names(&my expr, names, errs);
            flow->in_type_query = saved_type_query;
            flow->type_only_alias_ok = false;
            if (my expr_jsdoc_type) type_lookup_names(my expr_jsdoc_type, names, errs);
        }   break;

        case STATEMENT_THROW_EXPR:
            expr_lookup_names(&my expr, names, errs);
            flow_set_current(flow, &flow_unreachable);
            flow->has_flow_effects = true;
            break;

        case STATEMENT_RETURN_EXPR: {
            // tsc's checkReturnStatement: the containing function or class static block
            Scope const* container = names;
            while (container && container->kind != SCOPE_FUNCTION && container->kind != SCOPE_STATIC_BLOCK) {
                container = container->parent;
            }
            if (container && container->kind == SCOPE_STATIC_BLOCK) {
                error_ctx_grammar_error_d(errs, block_first_token(my span), 18041,
                    "A 'return' statement cannot be used inside a class static block.");
            } else if (!container) {
                error_ctx_error_d(
                    errs,
                    my expr.outer_span,
                    1108,
                    "A 'return' statement can only be used within a function body."
                );
            }
            expr_lookup_names(&my expr, names, errs);
            if (!flow->dead) {
                if (statement_is_bare_return(me)) flow->has_bare_return = true;
                flow->has_explicit_return = true;
                if (flow->return_target) flow_add_antecedent(flow, flow->return_target, flow->current);
                flow->current = &flow_unreachable;
                flow->has_flow_effects = true;
            }
        }   break;

        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE: {
            if (flow->dead) break;
            bool is_break = my tag == STATEMENT_BREAK;
            FlowNode* target = is_break ? flow->break_target : flow->continue_target;
            if (span_valid(my jump_label)) {
                target = nullptr;
                for (FlowActiveLabel* i = flow->active_labels; i; i = i->next) {
                    if (!csview_equals_sv(i->name.sv, my jump_label.sv)) continue;
                    i->referenced = true;
                    target = is_break ? i->break_target : i->continue_target;
                    break;
                }
            }
            if (target) {
                flow_add_antecedent(flow, target, flow->current);
                flow->current = &flow_unreachable;
                flow->has_flow_effects = true;
            }
        }   break;

        case STATEMENT_IMPORT_EXPORT: // Aliases are resolved by binder_link()
            port_statement_lookup_names(my import_export, names, errs);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_resolve_names(&my decl.variable, names, errs);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_lookup_names(my decl.function.impl, true, scope_new_self(names, my decl.function.impl->symbol, my decl.props.declared), errs);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_lookup_names(my decl.enum_, names, errs);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_lookup_names(my decl.class, scope_new_self(names, my decl.class->symbol, my decl.props.declared), errs);
            break;

        case STATEMENT_DECL_TYPE: {
            Scope* self_scope = scope_new_self(names, my decl.type.symbol, my decl.props.declared);
            unused_register(self_scope, UNUSED_TYPE_ALIAS, &my decl.type, span_null);
            my decl.type.outer_generics = names->generics;
            my decl.type.local = names->kind == SCOPE_BLOCK || names->kind == SCOPE_FUNCTION || names->kind == SCOPE_STATIC_BLOCK;
            Scope* generic_scope = scope_new_generic(self_scope, &my decl.type.generic, my decl.type.name, span_null, errs);
            type_lookup_names(&my decl.type.type, generic_scope, errs);
        }   break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_lookup_names(my decl.interface, scope_new_self(names, my decl.interface->symbol, my decl.props.declared), errs);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_resolve_names(&my decl.namespace, names, errs);
            break;

        case STATEMENT_SOURCE_DIRECTIVE:
            if (cstr_size(&my source_directive->not_found)) {
                // Without the quotes
                Span span = my source_directive->path_span;
                if (span.sv.size >= 2 && (*span.sv.buf == '"' || *span.sv.buf == '\'')) {
                    span.sv = csview_slice(span.sv, 1, span.sv.size - 1);
                }
                if (my source_directive->tag == SOURCE_DIRECTIVE_REFERENCE_TYPES) {
                    error_ctx_error_d(errs, span, 2688,
                        "Cannot find type definition file for '", my source_directive->not_found, "'.");
                } else {
                    error_ctx_error_d(errs, span, 6053,
                        "File '", my source_directive->not_found, "' not found.");
                }
            }
            break;

        case STATEMENT_NOOP:
            break;
    }
}

void statement_resolve_names(Statement* me, Scope* names, ErrorCtx* errs) {
    statement_resolve_names_labeled(me, 0, names, errs);
}

static void statement_resolve_names_labeled(Statement* me, u32 label_count, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    VecStatement const* list = flow->statement_list;
    flow->statement_list = nullptr;
    if (statement_check_unreachable(me, flow, list)) {
        // Like tsc: nothing in unreachable code is part of the flow graph
        bool was_dead = flow->dead;
        flow->dead = true;
        statement_resolve_names_reachable(me, label_count, names, errs);
        flow->dead = was_dead;
        return;
    }
    switch (my tag) {
        case STATEMENT_NOOP:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
        case STATEMENT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
        case STATEMENT_DECL_VARIABLE:
        case STATEMENT_WITH:
            my flow = flow_builder_reference(flow, span_null);
            break;
        default:
            break;
    }
    // What "the statement an assignment is in" can be (tsc: extendAssignmentPosition())
    switch (my tag) {
        case STATEMENT_DECL_VARIABLE:
        case STATEMENT_EXPR:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_WITH:
        case STATEMENT_DECL_CLASS: {
            FlowStatementFrame frame = { flow->statements, my span };
            flow->statements = &frame;
            statement_resolve_names_reachable(me, label_count, names, errs);
            flow->statements = frame.parent;
        }   break;

        default:
            statement_resolve_names_reachable(me, label_count, names, errs);
            break;
    }
}

void statement_collect_return_types(Statement const* me, TyBuf* out) {
    switch (my tag) {
        case STATEMENT_BLOCK: block_collect_return_types(&my block, out); break;

        case STATEMENT_IF_ELSE:
            for_each (i, vec_statement, &my if_else.statements) {
                statement_collect_return_types(i, out);
            }
            break;

        case STATEMENT_FOR_LOOP:
            statement_collect_return_types(&my for_loop->initialization, out);
            statement_collect_return_types(&my for_loop->body, out);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            statement_collect_return_types(&my for_each_loop->body, out);
            break;

        case STATEMENT_SWITCH:
            for_each (i, vec_switch_case, &my switch_->cases) {
                for_each (j, vec_statement, &i->statements) {
                    statement_collect_return_types(j, out);
                }
            }
            break;

        case STATEMENT_TRY_CATCH:
            block_collect_return_types(&my try_catch->try_block, out);
            block_collect_return_types(&my try_catch->catch_block, out);
            block_collect_return_types(&my try_catch->finally_block, out);
            break;

        case STATEMENT_RETURN_EXPR:
            if (statement_is_bare_return(me) || ty_is_recursive_return(current_checker, &my expr)) break;
            ty_buf_push(out, ty_check_expr(current_checker, &my expr, current_checker->return_context));
            break;

        case STATEMENT_LABELED: statement_collect_return_types(my labeled.body, out); break;
        case STATEMENT_WITH: statement_collect_return_types(&my with->body, out); break;

        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_THROW_EXPR:
        case STATEMENT_IMPORT_EXPORT:
        case STATEMENT_DECL_VARIABLE:
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_ENUM:
        case STATEMENT_DECL_CLASS:
        case STATEMENT_DECL_TYPE:
        case STATEMENT_DECL_INTERFACE:
        case STATEMENT_DECL_NAMESPACE:
            break;
    }
}

bool statement_any_return(Statement const* me, bool (*pred)(Expr const*)) {
    switch (my tag) {
        case STATEMENT_BLOCK: return statements_any_return(&my block.statements, pred);
        case STATEMENT_IF_ELSE: return statements_any_return(&my if_else.statements, pred);
        case STATEMENT_FOR_LOOP:
            return statement_any_return(&my for_loop->initialization, pred) || statement_any_return(&my for_loop->body, pred);
        case STATEMENT_FOR_EACH_LOOP: return statement_any_return(&my for_each_loop->body, pred);
        case STATEMENT_SWITCH:
            for_each (i, vec_switch_case, &my switch_->cases) {
                if (statements_any_return(&i->statements, pred)) return true;
            }
            return false;
        case STATEMENT_TRY_CATCH:
            return statements_any_return(&my try_catch->try_block.statements, pred) ||
                statements_any_return(&my try_catch->catch_block.statements, pred) ||
                statements_any_return(&my try_catch->finally_block.statements, pred);
        case STATEMENT_LABELED: return statement_any_return(my labeled.body, pred);
        case STATEMENT_WITH: return statement_any_return(&my with->body, pred);
        case STATEMENT_RETURN_EXPR: return !statement_is_bare_return(me) && pred(&my expr);
        default: return false;
    }
}

bool statements_any_return(VecStatement const* statements, bool (*pred)(Expr const*)) {
    for_each (i, vec_statement, statements) {
        if (statement_any_return(i, pred)) return true;
    }
    return false;
}

// tsc's checkGrammarBreakOrContinueStatement()
static void statement_check_jump(Statement const* me, BlockTypecheckCtx ctx, ErrorCtx* errs) {
    if (ctx.ambient_error_statement == me) return; // (tsc's checkGrammarStatementInAmbientContext comes first)
    bool is_break = my tag == STATEMENT_BREAK;
    bool labeled = span_valid(my jump_label);
    if (labeled) {
        for (TypecheckLabel const* i = ctx.labels; i; i = i->outer) {
            if (!csview_equals_sv(i->name.sv, my jump_label.sv)) continue;
            if (!is_break && !i->is_iteration) {
                error_ctx_grammar_error_d(errs, my span, 1115,
                    "A 'continue' statement can only jump to a label of an enclosing iteration statement.");
            }
            return;
        }
    } else if (is_break ? ctx.break_allowed : ctx.continue_allowed) {
        return;
    }
    if (ctx.in_function) {
        error_ctx_grammar_error_d(errs, my span, 1107, "Jump target cannot cross function boundary.");
    } else if (labeled && is_break) {
        error_ctx_grammar_error_d(errs, my span, 1116,
            "A 'break' statement can only jump to a label of an enclosing statement.");
    } else if (labeled) {
        error_ctx_grammar_error_d(errs, my span, 1115,
            "A 'continue' statement can only jump to a label of an enclosing iteration statement.");
    } else if (is_break) {
        error_ctx_grammar_error_d(errs, my span, 1105,
            "A 'break' statement can only be used within an enclosing iteration or switch statement.");
    } else {
        error_ctx_grammar_error_d(errs, my span, 1104,
            "A 'continue' statement can only be used within an enclosing iteration statement.");
    }
}

// tsc's checkReturnExpression: the branches of a conditional expression are checked one by one. An error that isn't
// elaborated goes on the `return` keyword (of a real return statement)
static bool return_typecheck(
    Statement const* statement, Expr const* expr, Ty const* target, bool in_conditional, bool awaited, ErrorCtx* errs
) {
    Checker* checker = current_checker;
    if (expr->tag == EXPR_TERNARY) {
        bool yes = return_typecheck(statement, &expr->ternary->yes, target, true, awaited, errs);
        bool no = return_typecheck(statement, &expr->ternary->no, target, true, awaited, errs);
        return yes && no;
    }
    Expr const* effective = expr_effective_check_node(expr);
    Span span = effective->inner_span;
    if (!in_conditional && span_valid(statement->span)) {
        span = span_make(statement->span.source, span_start_ptr(statement->span), span_start_ptr(statement->span) + 6);
    }
    Ty const* source = ty_expr_type(checker, expr);
    if (awaited) source = ty_awaited_returned(checker, source);
    return ty_check_assignable_expr(checker, source, target, span, effective, errs);
}

static FunctionImpl const* function_declaration(Statement const* me) {
    return me && my tag == STATEMENT_DECL_FUNCTION && !my decl.props.declared ? my decl.function.impl : nullptr;
}

void statements_check_overloads(VecStatement const* statements, ErrorCtx* errs) {
    isize count = vec_statement_size(statements);
    for (isize i = 0; i < count; i++) {
        FunctionImpl const* overload = function_declaration(vec_statement_at(statements, i));
        if (!overload || overload->has_body || !span_valid(overload->name)) continue;
        // A declaration file is ambient; after a parse error the body may be what the parser lost
        SourceFile const* file = overload->name.source;
        if (file->is_declaration || file->has_parse_errors) return;
        FunctionImpl const* next = i + 1 < count ? function_declaration(vec_statement_at(statements, i + 1)) : nullptr;
        if (next && csview_equals_sv(next->name.sv, overload->name.sv)) continue; // It goes on
        // Not followed by its implementation. An error if the list declares more of it later (tsc's consecutive
        // declarations), or if nothing declares more of it (the last declaration has no body)
        bool later_here = false;
        for (isize j = i + 1; j < count && !later_here; j++) {
            FunctionImpl const* other = function_declaration(vec_statement_at(statements, j));
            later_here = other && csview_equals_sv(other->name.sv, overload->name.sv);
        }
        // (Only a global merges across blocks: tsc checks a namespace's exported functions by the local symbol of
        // each block)
        Symbol const* symbol = overload->symbol && !overload->symbol->parent ? overload->symbol : nullptr;
        bool later_elsewhere = false;
        for (u32 d = 0; symbol && d < symbol->decl_count; d++) {
            if (symbol->decls[d].node != overload) continue;
            for (u32 e = d + 1; e < symbol->decl_count; e++) later_elsewhere |= symbol->decls[e].kind == SYMBOL_DECL_FUNCTION;
            break;
        }
        if (!later_here && later_elsewhere) continue;
        if (next && next->has_body) {
            error_ctx_error_d(errs, next->name, 2389, "Function implementation name must be '", overload->name, "'.");
        } else {
            error_ctx_error_d(errs, overload->name, 2391,
                "Function implementation is missing or not immediately following the declaration.");
        }
    }
}

// Whether a declaration of a function's symbol is ambient (tsc's getEffectiveDeclarationFlags & Ambient)
static bool function_symbol_decl_is_ambient(SymbolDecl const* decl) {
    switch (decl->kind) {
        case SYMBOL_DECL_FUNCTION: return ((FunctionImpl const*)decl->node)->ambient;
        case SYMBOL_DECL_CLASS: return ((ClassDecl const*)decl->node)->is_ambient;
        case SYMBOL_DECL_NAMESPACE: {
            NamespaceDecl const* namespace = decl->node;
            return namespace->inner_scope && namespace->inner_scope->ambient;
        }
        default: return decl->block && decl->block->ambient;
    }
}

// The ambient half of tsc's checkFlagAgreementBetweenOverloads (TS2384): when some but not all of the function's
// declarations are ambient, those that differ from the canonical one of their file (the implementation if it's in the
// same container as the file's first declaration, else that first declaration). TODO: TS2383 (export), which needs the
// export context of ambient declarations, and TS2385/TS2512/TS2386 for methods
static void function_check_ambient_agreement(Symbol const* symbol, FunctionImpl const* implementation, ErrorCtx* errs) {
    bool some = false, all = true;
    SymbolDecl const* implementation_decl = nullptr;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->node == implementation) implementation_decl = decl;
        if (decl->kind != SYMBOL_DECL_FUNCTION) continue;
        bool ambient = function_symbol_decl_is_ambient(decl);
        some |= ambient;
        all &= ambient;
    }
    if (some == all) return;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        SourceFile const* file = decl->name.source ? decl->name.source : decl->full.source;
        // The canonical declaration of its file
        SymbolDecl const* file_first = nullptr;
        for (u32 j = 0; j < symbol->decl_count && !file_first; j++) {
            SymbolDecl const* other = &symbol->decls[j];
            if ((other->name.source ? other->name.source : other->full.source) == file) file_first = other;
        }
        SymbolDecl const* file_canonical = implementation_decl && implementation_decl->block == file_first->block ?
            implementation_decl : file_first;
        bool ambient = function_symbol_decl_is_ambient(decl);
        if (ambient != function_symbol_decl_is_ambient(file_canonical)) {
            error_ctx_error_d(errs, symbol_decl_error_span(decl), 2384, "Overload signatures must all be ambient or non-ambient.");
        }
    }
}

// tsc's duplicateFunctionDeclaration: more than one declaration of the function has a body, reported on all of them.
// And that the overloads are compatible with the implementation (TS2394)
static void function_check_duplicate_implementation(FunctionImpl const* me, ErrorCtx* errs) {
    Symbol const* symbol = my symbol;
    if (!symbol || symbol_find_node(symbol, SYMBOL_DECL_FUNCTION) != me) return; // Once per symbol
    FunctionImpl const* implementation = nullptr;
    FunctionImpl const* overloads[32];
    Span names[32];
    u32 overload_count = 0;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_FUNCTION) continue;
        FunctionImpl const* function = symbol->decls[i].node;
        if (function->has_body) {
            if (!implementation) implementation = function;
        } else if (overload_count < c_arraylen(overloads)) {
            names[overload_count] = symbol->decls[i].name;
            overloads[overload_count++] = function;
        }
    }
    if (overload_count) function_check_ambient_agreement(symbol, implementation, errs);
    if (implementation && overload_count) {
        ty_check_overloads_compatible(current_checker, implementation, implementation->name, overloads, names, overload_count, errs);
    }
    // A function merged with a class that isn't ambient: TS2813 on each class, TS2814 on each function declaration
    bool non_ambient_class = false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_CLASS) continue;
        non_ambient_class |= !((ClassDecl const*)symbol->decls[i].node)->is_ambient;
    }
    for (u32 i = 0; non_ambient_class && i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        Error* error;
        if (decl->kind == SYMBOL_DECL_CLASS) {
            error = error_ctx_error_d(errs, decl->name, 2813, "Class declaration cannot implement overload list for '",
                symbol->is_default_export ? c_sv("default") : symbol->name, "'.");
        } else if (decl->kind == SYMBOL_DECL_FUNCTION) {
            error = error_ctx_error_d(errs, decl->name, 2814,
                "Function with bodies can only merge with classes that are ambient.");
        } else {
            continue;
        }
        for (u32 j = 0; j < symbol->decl_count; j++) {
            if (symbol->decls[j].kind != SYMBOL_DECL_CLASS) continue;
            error_add_related_d(error, symbol->decls[j].name, 6506, "Consider adding a 'declare' modifier to this class.");
        }
    }
    u32 bodies = 0;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind == SYMBOL_DECL_FUNCTION && ((FunctionImpl const*)symbol->decls[i].node)->has_body) bodies++;
    }
    if (bodies < 2) return;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_FUNCTION) continue;
        error_ctx_error_d(errs, symbol_decl_error_span(&symbol->decls[i]), 2393, "Duplicate function implementation.");
    }
}

static void statement_typecheck_inner(Statement* me, BlockTypecheckCtx ctx, ErrorCtx* errs);

void statement_typecheck(Statement* me, BlockTypecheckCtx ctx, ErrorCtx* errs) {
    CurrentNode saved = ty_enter_node(current_checker, my span); // tsc's checkSourceElement
    statement_typecheck_inner(me, ctx, errs);
    ty_leave_node(current_checker, saved);
}

static void statement_typecheck_inner(Statement* me, BlockTypecheckCtx ctx, ErrorCtx* errs) {
    // What the builder couldn't know: code after a call that never returns or an exhaustive switch (tsc's
    // checkSourceElement(); an error only when allowUnreachableCode is explicitly false)
    if (my flow && compiler_options()->is_set[COMPILER_OPTION_allow_unreachable_code] &&
        !compiler_options()->allow_unreachable_code && ty_flow_reachability(current_checker, my flow) == FLOW_REACH_NO
    ) {
        error_ctx_error_d(errs, my span, 7027, "Unreachable code detected.");
    }
    bool body = ctx.body;
    ctx.body = false;
    switch (my tag) {
        case STATEMENT_LABELED: {
            Statement const* labeled = my labeled.body;
            while (labeled->tag == STATEMENT_LABELED) labeled = labeled->labeled.body;
            TypecheckLabel label = {
                .name = my labeled.name,
                .is_iteration = labeled->tag == STATEMENT_FOR_LOOP || labeled->tag == STATEMENT_FOR_EACH_LOOP,
                .outer = ctx.labels,
            };
            ctx.labels = &label;
            ctx.inside_label = true;
            ctx.body = body;
            statement_typecheck(my labeled.body, ctx, errs);
        }   break;

        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
            break;

        case STATEMENT_WITH:
            // tsc's checkWithStatement: not the body (TS1300 is reported by the resolve walk)
            expr_typecheck(&my with->object, errs);
            error_ctx_grammar_error_d(errs, my with->header, 2410,
                "The 'with' statement is not supported. All symbols in a 'with' block will have type 'any'.");
            break;

        case STATEMENT_BLOCK:
            block_typecheck(&my block, ctx, errs);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&my if_else.statements);
            int cond_size = vec_expr_size(&my if_else.conditions);
            if (statement_size != cond_size && statement_size != cond_size + 1) {
                panic("invalid if statement has ", cond_size, " conditions and ", statement_size, " statements");
            }
            for (intptr_t i = 0; i < cond_size; i++) {
                // Conditions do not have to be booleans
                expr_typecheck_if_condition(vec_expr_at(&my if_else.conditions, i), vec_statement_at(&my if_else.statements, i), errs);
            }
            ctx.body = true;
            for_each (i, vec_statement, &my if_else.statements) {
                statement_typecheck(i, ctx, errs);
            }
            break;
        }

        case STATEMENT_FOR_LOOP: {
            statement_typecheck(&my for_loop->initialization, ctx, errs);
            expr_typecheck(&my for_loop->condition, errs);
            expr_check_truthiness(&my for_loop->condition, errs);
            expr_typecheck(&my for_loop->afterthought, errs);
            ctx.break_allowed = true;
            ctx.continue_allowed = true;
            ctx.body = true;
            statement_typecheck(&my for_loop->body, ctx, errs);
            break;
        }

        case STATEMENT_FOR_EACH_LOOP: {
            Checker* checker = current_checker;
            Ty const* iterable_type = ty_expr_type(checker, &my for_each_loop->iterable);

            // TODO: `for await` in an async function below ES2018 (__asyncValues) needs the function it is in
            if (my for_each_loop->keyword == FOR_EACH_LOOP_OF && !my for_each_loop->is_await &&
                compiler_options()->downlevel_iteration && compiler_options()->target < SCRIPT_TARGET_ES2015
            ) {
                ty_check_emit_helpers(checker, my span, EMIT_HELPER_VALUES, errs);
            }
            if (my for_each_loop->variable.tag == FOR_EACH_LOOP_STATEMENT) {
                variable_statement_check_using(&my for_each_loop->variable.statement,
                    my for_each_loop->keyword == FOR_EACH_LOOP_IN, errs);
            }
            switch (my for_each_loop->keyword) {
                case FOR_EACH_LOOP_IN:
                    switch (my for_each_loop->variable.tag) {
                        case FOR_EACH_LOOP_STATEMENT:
                            if (!vec_initialized_binding_is_empty(&my for_each_loop->variable.statement.initializers) &&
                                my for_each_loop->variable.statement.initializers.data->binding.tag != BINDING_IDENTIFIER
                            ) {
                                error_ctx_error_d(
                                    errs,
                                    my for_each_loop->variable.statement.initializers.data->binding.content_span,
                                    2491,
                                    "The left-hand side of a 'for...in' statement cannot be a destructuring pattern."
                                );
                            }
                            break;

                        case FOR_EACH_LOOP_EXPR:
                            if (my for_each_loop->variable.expr.tag == EXPR_ARRAY ||
                                my for_each_loop->variable.expr.tag == EXPR_OBJECT
                            ) {
                                error_ctx_error_d(
                                    errs,
                                    my for_each_loop->variable.expr.outer_span,
                                    2491,
                                    "The left-hand side of a 'for...in' statement cannot be a destructuring pattern."
                                );
                            }
                            break;
                    }

                    // tsc's checkForInStatement: null and undefined are removed first (getNonNullableTypeIfNeeded)
                    Ty const* right_type = compiler_options()->strict_null_checks ?
                        ty_non_nullable(current_checker, iterable_type) : iterable_type;
                    if ((right_type->flags & TYF_NEVER) || !for_in_accepts(current_checker, right_type)) {
                        error_ctx_error_d(errs, my for_each_loop->iterable.outer_span, 2407,
                            "The right-hand side of a 'for...in' statement must be of type 'any', an object type "
                            "or a type parameter, but here has type '", right_type, "'.");
                    }
                    break;

                case FOR_EACH_LOOP_OF:
                    ty_iterated_type(checker, my for_each_loop->is_await ? ITERATION_FOR_AWAIT_OF : ITERATION_FOR_OF,
                        iterable_type, my for_each_loop->iterable.outer_span, errs);
                    break;
            }

            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    for_each (i, vec_initialized_binding, &my for_each_loop->variable.statement.initializers) {
                        variable_statement_check_let_names(&my for_each_loop->variable.statement, i, errs);
                        binding_typecheck(&i->binding, nullptr, errs);
                    }
                    break;

                case FOR_EACH_LOOP_EXPR:
                    expr_typecheck(&my for_each_loop->variable.expr, errs);
                    if (!my for_each_loop->variable.expr.assignment_pattern) {
                        bool in = my for_each_loop->keyword == FOR_EACH_LOOP_IN;
                        expr_check_reference_not_optional(&my for_each_loop->variable.expr, in ? 2780 : 2781, in ?
                            "The left-hand side of a 'for...in' statement may not be an optional property access." :
                            "The left-hand side of a 'for...of' statement may not be an optional property access.", errs);
                    }
                    if (my for_each_loop->variable.expr.assignment_pattern) {
                        // Destructured element by element (tsc's checkDestructuringAssignment). A for..in pattern is
                        // TS2491 and nothing else
                        expr_destructuring_typecheck(&my for_each_loop->variable.expr,
                            my for_each_loop->keyword == FOR_EACH_LOOP_IN ? nullptr :
                            ty_widened_objects(checker, for_each_loop_statement_pattern_source_type(my for_each_loop)), errs);
                        break;
                    }
                    if (my for_each_loop->keyword == FOR_EACH_LOOP_IN) {
                        // tsc's checkForInStatement: one message whatever the variable's type
                        if (!ty_is_related(checker, lazy_type_get(&my for_each_loop->var_type),
                                expr_get_type(&my for_each_loop->variable.expr), TY_RELATION_ASSIGNABLE)
                        ) {
                            error_ctx_error_d(errs, my for_each_loop->variable.expr.outer_span, 2405,
                                "The left-hand side of a 'for...in' statement must be of type 'string' or 'any'.");
                        }
                        break;
                    }
                    ty_check_assignable_expr(
                        checker,
                        // HACK: tsc checks a pattern target property by property (checkDestructuringAssignment),
                        // so the elements get no excess property checks
                        ty_widened_objects(checker, lazy_type_get(&my for_each_loop->var_type)),
                        expr_get_type(&my for_each_loop->variable.expr),
                        my for_each_loop->variable.expr.outer_span,
                        &my for_each_loop->iterable,
                        errs
                    );
                    break;
            }
            // (After the variables, like tsc: whether it's assigned walks the loop, whose variables it then types)
            expr_typecheck(&my for_each_loop->iterable, errs);

            ctx.break_allowed = true;
            ctx.continue_allowed = true;
            ctx.body = true;
            statement_typecheck(&my for_each_loop->body, ctx, errs);
        }   break;

        case STATEMENT_SWITCH: {
            expr_typecheck(&my switch_->needle, errs);
            Ty const* needle_type = expr_get_type(&my switch_->needle);
            intptr_t default_count = 0;
            ctx.break_allowed = true;
            for_each (i, vec_switch_case, &my switch_->cases) {
                if (i->is_pattern) {
                    expr_typecheck(&i->pattern, errs);
                    Ty const* pattern_type = expr_get_type(&i->pattern);
                    // tsc's isTypeEqualityComparableTo: a nullable case type is always fine, else the switch's type
                    // comparable to the case's, or else the case's to the switch's (checkTypeComparableTo, reported)
                    if (!(pattern_type->flags & TYF_NULLABLE)
                        && !ty_is_comparable_either_way(current_checker, needle_type, pattern_type)) {
                        ty_check_comparable(current_checker, pattern_type, needle_type, i->pattern.outer_span, errs);
                    }
                } else {
                    default_count++;
                    if (default_count == 2) {
                        error_ctx_error_d(
                            errs,
                            i->default_span,
                            1113,
                            "A 'default' clause cannot appear more than once in a 'switch' statement."
                        );
                    }
                }
                for_each (j, vec_statement, &i->statements) {
                    statement_typecheck(j, ctx, errs);
                }
                if (i->fallthrough && ty_flow_reachability(current_checker, i->fallthrough) == FLOW_REACH_YES) {
                    error_ctx_error_d(errs, i->head, 7029, "Fallthrough case in switch.");
                }
            }
        }   break;

        case STATEMENT_TRY_CATCH:
            block_typecheck(&my try_catch->try_block, ctx, errs);
            binding_typecheck(&my try_catch->catch_var, nullptr, errs);
            if (my try_catch->catch_var.has_explicit_type &&
                !(ty_of_node(&my try_catch->catch_var.explicit_type)->flags & (TYF_ANY | TYF_UNKNOWN))
            ) {
                error_ctx_error_d(errs, my try_catch->catch_var.explicit_type_span, 1196,
                    "Catch clause variable type annotation must be 'any' or 'unknown' if specified.");
            }
            block_typecheck(&my try_catch->catch_block, ctx, errs);
            block_typecheck(&my try_catch->finally_block, ctx, errs);
            break;

        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
            statement_check_jump(me, ctx, errs);
            break;

        case STATEMENT_THROW_EXPR:
            if (my expr.tag == EXPR_IMPLICIT_ANY && span_valid(my expr.inner_span) && !my expr.inner_span.sv.size &&
                span_valid(my span)
            ) {
                // tsc's checkThrowStatement: `throw` then a line break (the parser's missing expression), right after
                // the keyword
                char const* at = span_start_ptr(my span) + strlen("throw");
                error_ctx_grammar_error_d(errs, span_make(my span.source, at, at), 1142, "Line break not permitted here.");
            }
            expr_typecheck(&my expr, errs);
            break;

        case STATEMENT_EXPR:
            expr_typecheck(&my expr, errs);
            break;

        case STATEMENT_EXPORT_DEFAULT_EXPR:
            if (my expr_jsdoc_type) {
                // (Checked with it as the contextual type, then related without elaborating: tsc's checkTypeAssignableTo)
                type_typecheck(my expr_jsdoc_type, errs);
                Ty const* declared = ty_of_node(my expr_jsdoc_type);
                Ty const* type = ty_check_expr(current_checker, &my expr, declared->flags & TYF_ANY ? nullptr : declared);
                expr_typecheck(&my expr, errs);
                ty_check_assignable(current_checker, type, declared, my expr.outer_span, errs);
                break;
            }
            expr_typecheck(&my expr, errs);
            break;

        case STATEMENT_RETURN_EXPR:
            ty_check_expr(current_checker, &my expr, current_checker->return_context);
            expr_typecheck(&my expr, errs);
            if (ctx.in_setter) {
                if (!statement_is_bare_return(me)) {
                    error_ctx_error_d(errs, span_make(my span.source, span_start_ptr(my span), span_start_ptr(my span) + 6), 2408,
                        "Setters cannot return a value.");
                }
            } else if (ctx.constructed_class && !statement_is_bare_return(me)) {
                // What a constructor returns replaces the instance (tsc's checkReturnStatement)
                if (!return_typecheck(me, &my expr, ty_instance_type_of_class(current_checker, ctx.constructed_class), false, false, errs)) {
                    error_ctx_error_d(errs, span_make(my span.source, span_start_ptr(my span), span_start_ptr(my span) + 6), 2409,
                        "Return type of constructor signature must be assignable to the instance type of the class.");
                }
            } else if (ctx.unwrapped_return) {
                if (!statement_is_bare_return(me)) {
                    return_typecheck(me, &my expr, ctx.unwrapped_return, false, ctx.await_returned, errs);
                }
            } else if (ctx.specified_return) {
                return_typecheck(me, &my expr, ty_of_node(ctx.specified_return), false, false, errs);
            }
            // tsc's checkReturnStatement: a bare `return;` where the function returns something
            if (statement_is_bare_return(me) && ctx.function && !ctx.constructed_class && compiler_options()->no_implicit_returns &&
                !compiler_options()->strict_null_checks
            ) {
                Ty const* returned = ty_function_return_type(current_checker, ctx.function);
                if (!(returned->flags & TYF_NEVER) && !function_impl_return_void_any_or_undefined(ctx.function, returned)) {
                    error_ctx_error_d(errs, span_make(my span.source, span_start_ptr(my span), span_start_ptr(my span) + 6), 7030,
                        "Not all code paths return a value.");
                }
            }
            break;

        case STATEMENT_IMPORT_EXPORT:
            port_statement_typecheck(my import_export, my span, ctx.ambient, errs);
            break;

        case STATEMENT_DECL_VARIABLE:
            if ((my decl.variable.keyword == VAR_INIT_USING || my decl.variable.keyword == VAR_INIT_AWAIT_USING) &&
                compiler_options()->target < SCRIPT_TARGET_ESNEXT && !ctx.ambient
            ) {
                ty_check_emit_helpers(current_checker, my decl.variable.span, EMIT_HELPER_DISPOSABLE, errs);
            }
            if (body && my decl.variable.keyword != VAR_INIT_VAR) {
                // tsc's checkGrammarForDisallowedBlockScopedVariableStatement()
                error_ctx_grammar_error_d(errs, span_with_semicolon(my span), 1156,
                    "'", variable_statement_keyword_str(&my decl.variable), "' declarations can only be declared inside a block.");
            }
            variable_statement_typecheck(&my decl.variable, my decl.props.declared || ctx.ambient, my decl.props.exported, errs);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_typecheck(my decl.function.impl, errs);
            function_impl_check_implicit_any_return(my decl.function.impl, my decl.function.impl->name, my decl.function.impl->name, errs);
            function_check_duplicate_implementation(my decl.function.impl, errs);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_typecheck(my decl.enum_, errs);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_typecheck(my decl.class, errs);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_typecheck(&my decl.type, errs);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_typecheck(my decl.interface, errs);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_typecheck(&my decl.namespace, my decl.props.declared || ctx.ambient, errs);
            break;
    }
}

void switch_case_drop(SwitchCase* me) {
    if (my is_pattern) {
        expr_drop(&my pattern);
    }
    vec_statement_drop(&my statements);
}
