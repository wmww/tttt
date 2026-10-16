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

static void export_assignment_check_isolated(Statement const* me, Scope* names, ErrorCtx* errs);

// Variable initialization

void variable_statement_drop(VariableStatement* me) {
    vec_initialized_binding_drop(&my initializers);
}

void variable_statement_add_names(VariableStatement* me, Scope* scope, DeclStatementProps props) {
    for_each (i, vec_initialized_binding, &my initializers) {
        initialized_binding_add_names(i, scope, (BindingAddNamesProps){
            .abstract_literals = !variable_statement_is_const(me),
            .exported = props.exported != DECL_EXPORTED_NO,
            .function_scoped = my keyword == VAR_INIT_VAR,
            .is_const = variable_statement_is_const(me),
            .is_ambient = props.declared,
            .declare_keyword = span_valid(props.declare_keyword),
        });
    }
}

// tsc's checkAwaitGrammar for an `await using` in a function that isn't async, with the function to mark async
static void variable_statement_check_await_place(VariableStatement const* me, Scope* names, ErrorCtx* errs) {
    if (my keyword == VAR_INIT_AWAIT_USING && my in_sync_function && !my in_static_block) {
        await_check_grammar(AWAIT_IN_FUNCTION, AWAIT_KIND_USING, variable_statement_await_span(me), scope_containing_function(names), errs);
    }
}

void variable_statement_resolve_names(VariableStatement* me, Scope* names, ErrorCtx* errs) {
    variable_statement_check_await_place(me, names, errs);
    for_each (i, vec_initialized_binding, &my initializers) {
        initialized_binding_lookup_names(i, names, errs);
    }
}

// The grammar of `using` declarations: tsc's checkGrammarVariableDeclarationList(), checkAwaitGrammar() and the using
// part of checkGrammarVariableDeclaration()
// tsc's checkForOfStatement checks the iterable through the variable declarations: `for (var of x)` (TS1123) has none
static bool for_of_checks_iterable(ForEachLoopStatement const* loop) {
    return loop->keyword != FOR_EACH_LOOP_OF || loop->variable.tag != FOR_EACH_LOOP_STATEMENT ||
        !vec_initialized_binding_is_empty(&loop->variable.statement.initializers);
}

static void variable_statement_check_using(VariableStatement* me, bool for_in, bool case_clause, bool ambient, ErrorCtx* errs) {
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
    if (case_clause) {
        error_ctx_grammar_error_d(errs, my span, is_await ? 1548 : 1547, "'", variable_statement_keyword_str(me),
            "' declarations are not allowed in 'case' or 'default' clauses unless contained within a block.");
        return;
    }
    if (ambient) {
        error_ctx_grammar_error_d(errs, my span, is_await ? 1546 : 1545, "'", variable_statement_keyword_str(me),
            "' declarations are not allowed in ambient contexts.");
        return;
    }
    if (is_await && my in_static_block) {
        error_ctx_error_d(errs, my span, 18054, // Regardless of parse errors, like tsc
            "'await using' statements cannot be used inside a class static block.");
    } else if (is_await) {
        // (In a function the parser reported it)
        await_check_grammar(my at_top_level ? AWAIT_AT_TOP_LEVEL : AWAIT_ALLOWED, AWAIT_KIND_USING, variable_statement_await_span(me), nullptr, errs);
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
            if (csview_equals(span_sv(my content_span), "let")) {
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

// tsc's checkESModuleMarker: the first name a binding declares
static void binding_check_es_module_marker(Binding const* me, ErrorCtx* errs) {
    switch (my tag) {
        case BINDING_IDENTIFIER:
            if (csview_equals(span_sv(my content_span), "__esModule")) {
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

void variable_statement_typecheck(VariableStatement* me, bool declared, bool exported, bool case_clause, bool ambient_using, ErrorCtx* errs) {
    variable_statement_check_using(me, false, case_clause, ambient_using, errs);
    // An export of a module emitted as CommonJS (or AMD, UMD) can't be named like the marker the emit adds
    bool check_marker = exported && !declared && !compiler_options()->no_emit && !vec_initialized_binding_is_empty(&my initializers) &&
        source_file_emit_module_kind(span_source(my initializers.data[0].binding.content_span)) < MODULE_KIND_SYSTEM;
    for_each (i, vec_initialized_binding, &my initializers) {
        if (check_marker) binding_check_es_module_marker(&i->binding, errs);
        // An ambient, exported or const variable without a type has nothing for control flow to type it by (tsc's
        // getTypeForVariableLikeDeclaration: no autoType)
        if ((declared || exported || variable_statement_is_const(me)) && compiler_options()->no_implicit_any && i->binding.tag == BINDING_IDENTIFIER &&
            !i->binding.has_explicit_type && initialized_binding_value(i)->tag == EXPR_IMPLICIT_ANY &&
            !initialized_binding_value(i)->missing // (`const x = ;`: tsc's missing identifier, an error type)
        ) {
            error_ctx_error_d(errs, i->binding.content_span, 7005, "Variable '", i->binding.content_span,
                "' implicitly has an 'any' type.");
        }
        if (initialized_binding_value(i)->tag == EXPR_IMPLICIT_ANY && !declared &&
            (i->binding.tag == BINDING_ARRAY || i->binding.tag == BINDING_OBJECT) &&
            my keyword != VAR_INIT_USING && my keyword != VAR_INIT_AWAIT_USING // (TS1492 instead, variable_statement_check_using())
        ) {
            // tsc's checkGrammarVariableDeclaration
            error_ctx_grammar_error_d(errs, i->binding.content_span, 1182, "A destructuring declaration must have an initializer.");
            // The pattern is all it goes by (tsc's getWidenedTypeForVariableLikeDeclaration with reportErrors)
            if (!i->binding.has_explicit_type && compiler_options()->no_implicit_any) {
                binding_add_implicit_any_type_errors(&i->binding, span_null, false, errs);
            }
        } else if (variable_statement_is_const(me) &&
            initialized_binding_value(i)->tag == EXPR_IMPLICIT_ANY &&
            !declared
        ) {
            error_ctx_grammar_error_d(errs, i->binding.content_span, 1155,
                "'", variable_statement_keyword_str(me), "' declarations must be initialized.");
        } else if (declared &&
            initialized_binding_value(i)->tag != EXPR_IMPLICIT_ANY
        ) {
            if (i->binding.has_explicit_type || !variable_statement_is_const(me)) {
                error_ctx_grammar_error_d(errs, expr_outer_span(initialized_binding_value(i)), 1039,
                    "Initializers are not allowed in ambient contexts.");
            } else {
                bool valid = expr_is_ambient_const_initializer(initialized_binding_value(i));
                if (!valid) {
                    error_ctx_grammar_error_d(errs, expr_outer_span(initialized_binding_value(i)), 1254,
                        "A 'const' initializer in an ambient context must be a string or numeric literal or literal enum reference.");
                }
                if (valid) variable_statement_check_let_names(me, i, errs);
            }
        } else {
            variable_statement_check_let_names(me, i, errs);
        }
        initialized_binding_typecheck(i, errs);
        if ((my keyword == VAR_INIT_USING || my keyword == VAR_INIT_AWAIT_USING) && initialized_binding_value(i)->tag != EXPR_IMPLICIT_ANY) {
            // tsc widens it first: no excess property checks
            Ty const* type = ty_widened_objects(current_checker, expr_get_type(initialized_binding_value(i)));
            ty_check_using_initializer(current_checker, type, my keyword == VAR_INIT_AWAIT_USING,
                expr_outer_span(initialized_binding_value(i)), errs);
        }
    }
}

// tsc's checkAmbientInitializer: what a const or readonly declaration without a type may be initialized with where
// nothing is emitted: a literal (`-1`, `-1n`: isStringOrNumberLiteralExpression, isBigIntLiteralExpression) or a
// reference to an enum member (`E.A`, `E["A"]`: isSimpleLiteralEnumReference), unparenthesized
bool expr_is_ambient_const_initializer(Expr const* me) {
    if (my paren_before) return false;
    switch (my tag) {
        case EXPR_LITERAL:
            return my literal.tag == VALUE_NUMBER || my literal.tag == VALUE_STRING || my literal.tag == VALUE_BOOLEAN ||
                my literal.tag == VALUE_BIGINT;
        case EXPR_UNARY_OP: {
            Expr const* operand = &my unary_op->expr;
            return my unary_op->op == UNARY_OP_MINUS && operand->tag == EXPR_LITERAL && !operand->paren_before &&
                (operand->literal.tag == VALUE_NUMBER || operand->literal.tag == VALUE_BIGINT);
        }
        case EXPR_PROPERTY: {
            PropertyExpr const* property = my property;
            if (property->form == PROPERTY_EXPR_BRACKET && !(property->index.tag == EXPR_LITERAL && !property->index.paren_before &&
                (property->index.literal.tag == VALUE_STRING || property->index.literal.tag == VALUE_NUMBER))
            ) {
                return false;
            }
            if (!expr_is_entity_name(&property->expr)) return false;
            return (ty_expr_type(current_checker, me)->flags & (TYF_ENUM_LITERAL | TYF_ENUM)) != 0;
        }
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
    if (my jsdoc_check_only) return;
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = my jsdoc && scope->kind == SCOPE_MODULE ? DECL_EXPORTED_YES : props.exported,
        .kind = SYMBOL_DECL_TYPE_ALIAS,
        .node = me,
        .declare_keyword = span_valid(props.declare_keyword),
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
    } else if (my symbol && my type.tag == TYPE_TEMPLATE) {
        // (A template body: TS2590 at the template, getTemplateLiteralType's checkCrossProductUnion)
        CurrentNode saved = ty_enter_node(current_checker, my type.template_->span);
        ty_declared_type_of(current_checker, my symbol);
        ty_leave_node(current_checker, saved);
    } else if (my symbol && (my type.tag == TYPE_UNION || my type.tag == TYPE_INTERSECTION) && !my jsdoc) {
        // (A union or intersection body, after its members (tsc's checkUnionOrIntersectionType): TS2590 of a cross
        // product at it, from the `=` to the end without the `;`)
        type_typecheck(&my type, errs);
        isize items = vec_generic_item_size(&my generic.items);
        char const* start = span_end_ptr(items ? vec_generic_item_at(&my generic.items, items - 1)->full_span : my name);
        char const* end = span_end_ptr(my full_span);
        while (start < end && *start != '=') start++;
        if (start < end) start++;
        while (start < end && strchr(" \t\r\n", *start)) start++;
        while (end > start && strchr("; \t\r\n", end[-1])) end--;
        CurrentNode saved = ty_enter_node(current_checker, span_make_in(my full_span, start, end));
        ty_declared_type_of(current_checker, my symbol);
        ty_leave_node(current_checker, saved);
        return;
    } else if (my symbol && my type.tag == TYPE_TUPLE && span_valid(my type.tuple.span)) {
        // (Likewise a tuple body, where the elements are spread: TS2799 at the tuple node)
        CurrentNode saved = ty_enter_node(current_checker, my type.tuple.span);
        ty_declared_type_of(current_checker, my symbol);
        ty_leave_node(current_checker, saved);
    }
    // (tsc's checkTypeAliasDeclaration: only the compiler's intrinsic types, with one type parameter)
    if (my type.tag == TYPE_IDENTIFIER && my type.identifier->intrinsic_keyword) {
        static char const* const kinds[] = { "Uppercase", "Lowercase", "Capitalize", "Uncapitalize", "NoInfer" };
        bool known = false;
        for (u32 i = 0; i < c_arraylen(kinds) && !known; i++) known = csview_equals(span_sv(my name), kinds[i]);
        isize count = vec_generic_item_size(&my generic.items);
        if (count == 0 ? !csview_equals(span_sv(my name), "BuiltinIteratorReturn") : count != 1 || !known) {
            error_ctx_error_d(errs, my type.identifier->name, 2795,
                "The 'intrinsic' keyword can only be used to declare compiler provided intrinsic types.");
        }
    }
    type_typecheck(&my type, errs);
}

// Namespace declaration

void namespace_decl_drop(NamespaceDecl* me) {
    block_drop(&my contents);
    if (my attributes) {
        type_drop(my attributes);
        ast_delete(my attributes);
    }
}

// The blocks of the namespaces around the one being looked at, innermost first (tsc walks up the parent nodes)
typedef struct BlockChain { Block const* block; struct BlockChain const* parent; } BlockChain;

static bool block_is_instantiated_in(Block const* me, bool const_enums, Scope const* outer, BlockChain const* enclosing, u32 depth);
static bool block_is_instantiated(Block const* me, bool const_enums, Scope const* outer, u32 depth) {
    return block_is_instantiated_in(me, const_enums, outer, nullptr, depth);
}

typedef enum InstanceState { NON_INSTANTIATED, CONST_ENUM_ONLY, INSTANTIATED } InstanceState;

// The instance state of a statement that declares `name`, NON_INSTANTIATED if it declares nothing of that name (tsc's
// nodeHasName and getModuleInstanceStateCached). found: whether it declares it
static InstanceState statement_instance_state_of(Statement const* me, csview name, Scope const* outer, u32 depth, bool* found) {
    *found = false;
    switch (my tag) {
        case STATEMENT_DECL_TYPE:
            *found = csview_equals_sv(span_sv(my decl.type->name), name);
            return NON_INSTANTIATED;
        case STATEMENT_DECL_INTERFACE:
            *found = csview_equals_sv(span_sv(my decl.interface->name), name);
            return NON_INSTANTIATED;
        case STATEMENT_DECL_ENUM:
            *found = csview_equals_sv(span_sv(my decl.enum_->name), name);
            return my decl.enum_->is_const ? CONST_ENUM_ONLY : INSTANTIATED;
        case STATEMENT_DECL_FUNCTION:
            *found = csview_equals_sv(span_sv(my decl.function.impl->name), name);
            return INSTANTIATED;
        case STATEMENT_DECL_CLASS:
            *found = csview_equals_sv(span_sv(my decl.class->name), name);
            return INSTANTIATED;
        case STATEMENT_DECL_VARIABLE:
            for_each (i, vec_initialized_binding, &my decl.variable.initializers) {
                if (i->binding.tag == BINDING_IDENTIFIER && csview_equals_sv(span_sv(i->binding.content_span), name)) *found = true;
            }
            return INSTANTIATED;
        case STATEMENT_DECL_NAMESPACE:
            *found = !my decl.namespace->is_ambient_module && csview_equals_sv(span_sv(my decl.namespace->name), name);
            if (!*found) return NON_INSTANTIATED;
            return block_is_instantiated(&my decl.namespace->contents, true, outer, depth + 1) ? INSTANTIATED : NON_INSTANTIATED;
        case STATEMENT_IMPORT_EXPORT: {
            // A re-export of `import x = N.x` or `import x = require("m")` is instantiated, it's ambiguous. Other imports
            // have no name to tsc's nodeHasName (an ImportDeclaration's names are in its clause): not found
            // (Any import-equals declaration, `import x = require("m")` too: tsc's ImportEqualsDeclaration)
            PortStatement const* port = my import_export;
            bool import_equals = port->main.tag == PORT_IMPORT_ALIAS || (port->main.tag == PORT_IMPORT_GATHER && port->main.is_require);
            *found = import_equals && csview_equals_sv(port_item_name(port->main.decl_name), name);
            return *found ? INSTANTIATED : NON_INSTANTIATED;
        }
        default:
            return NON_INSTANTIATED;
    }
}

// tsc's getModuleInstanceStateForAliasTarget: `export { a }` is as instantiated as the declarations of `a` in the
// nearest block that has any (`me`, then the blocks of the scopes around it). Instantiated if there are none
static InstanceState alias_target_state(Block const* me, csview name, Scope const* outer, BlockChain const* enclosing, u32 depth) {
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
        // (The enclosing namespaces' blocks first, then the scopes around the outermost one)
        if (enclosing) {
            block = enclosing->block;
            enclosing = enclosing->parent;
            continue;
        }
        while (scope && (!scope->block || scope->block == block)) scope = scope->parent;
        block = scope ? scope->block : nullptr;
    }
    return INSTANTIATED;
}

// tsc's getModuleInstanceState(), syntactically: a namespace is a value if it contains more than types
// const_enums: whether a `const enum` counts (it does for the symbol, for unreachable code only when it is emitted).
// outer: the scope the namespace is declared in, where the names it exports without declaring are looked up
static bool block_is_instantiated_in(Block const* me, bool const_enums, Scope const* outer, BlockChain const* enclosing, u32 depth) {
    BlockChain here = { me, enclosing };
    for_each (i, vec_statement, &my statements) {
        switch (i->tag) {
            case STATEMENT_NOOP:
                // `;` and `debugger` are statements like any other to tsc's getModuleInstanceState, what the parser
                // made of an error isn't there
                if (span_valid(i->span) && i->span.size && (i->span.buf[0] == ';' || i->span.buf[0] == 'd')) return true;
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
                        InstanceState state = alias_target_state(me, port_item_name(source), outer, enclosing, depth);
                        if (state == INSTANTIATED || (state == CONST_ENUM_ONLY && const_enums)) return true;
                    }
                    break;
                }
                if (port_item_is_export(&port->main) || port->main.export_import || is_export) return true;
                break;
            }

            case STATEMENT_DECL_NAMESPACE:
                // (A `declare global` too, by its contents: tsc's getModuleInstanceState)
                if (block_is_instantiated_in(&i->decl.namespace->contents, const_enums, outer, &here, depth + 1)) {
                    return true;
                }
                break;

            default:
                return true;
        }
    }
    return false;
}

static bool scope_in_script_block(Scope const* scope);
void namespace_decl_add_names(NamespaceDecl* me, Scope* scope, DeclStatementProps props) {
    // tsc's checkGrammarModuleElementContext: directly in a file or a namespace body, else only this is said about it
    bool misplaced = !my jsdoc && (my labeled || !(scope->kind == SCOPE_SCRIPT || scope->kind == SCOPE_MODULE ||
        scope->kind == SCOPE_NAMESPACE || scope->kind == SCOPE_GLOBAL_AUGMENTATION));
    // (Exported from an ES module, a local of a CommonJS one: tsc's declareSourceFileMember with isExternalModule)
    if (my jsdoc && scope->kind == SCOPE_MODULE && !(span_source(my full_span) && span_source(my full_span)->is_commonjs)) {
        props.exported = DECL_EXPORTED_YES;
    }
    my misplaced = misplaced;
    ErrorCtx* errs = misplaced ? nullptr : binder_errs(scope->binder);
    bool checked = true;
    for (Scope const* i = scope; i && checked; i = i->parent) checked = !i->with_body;
    if (misplaced && checked) {
        Span first = my full_span;
        isize len = 0;
        while (len < first.size && (isalnum((unsigned char)first.buf[len]) || first.buf[len] == '_')) len++;
        first.size = len;
        error_ctx_grammar_error_d(binder_errs(scope->binder), first, (my is_global || my is_ambient_module) ? 1234 : 1235,
            (my is_global || my is_ambient_module) ? "An ambient module declaration is only allowed at the top level in a file." :
            "A namespace declaration is only allowed at the top level of a namespace or module.");
    }
    // tsc's bindModuleDeclaration: an ambient module (not an augmentation) is a pattern with at most one `*`
    if (my is_ambient_module && my module_ref && scope->kind != SCOPE_MODULE && errs) {
        csview path = cstr_sv(&my module_ref->path);
        char const* star = memchr(path.buf, '*', (size_t)path.size);
        if (star && memchr(star + 1, '*', (size_t)(path.buf + path.size - star - 1))) {
            error_ctx_error_d(errs, my name, 5061, "Pattern '", path, "' can have at most one '*' character.");
        } else if (!star && my attributes) {
            error_ctx_error_d(errs, my name, 1550, "An ambient module declaration with import attributes must use a pattern "
                "name with an '*' character.");
        }
    }
    // tsc's bindModuleDeclaration: an ambient module or augmentation is visible anyway (on the first token, `export`)
    if ((my is_global || my is_ambient_module) && props.exported && errs) {
        error_ctx_error_d(errs, block_first_token(my full_span), 2668, "'export' modifier cannot be applied to ambient modules "
            "and module augmentations since they are always visible.");
    }
    if (my is_global && !props.declared && !scope->ambient) {
        error_ctx_error_d(errs, my name, 2670,
            "Augmentations for the global scope should have 'declare' modifier unless they appear in already ambient context.");
    }

    // tsc's checkModuleDeclaration: where `declare global` and `declare module "m"` can be. Augmentations
    // (isModuleAugmentationExternal) are at the top level of a module or directly in an ambient module of a script,
    // an ambient module can also be at the top level of a script, not a global augmentation
    bool invalid_global = false, nested_ambient_module = false;
    bool in_script_ambient_module = scope->kind == SCOPE_NAMESPACE && scope->container &&
        scope->container->is_module && scope->parent && scope->parent->kind == SCOPE_SCRIPT;
    if (my is_global || my is_ambient_module) {
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
            nested_ambient_module = true;
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
            // (Directly in an ambient module of a script too: tsc's isModuleAugmentationExternal)
            bool augmentation = scope->kind == SCOPE_MODULE || in_script_ambient_module;
            if (augmentation && !my is_shorthand) {
                module = binder_new_symbol(scope->binder, span_sv(my name), SYMBOL_VALUE_MODULE);
                module->name_span = my name;
                module->exports = arena_new(binder_arena(scope->binder), SymbolTable);
                module->is_augmentation = true;
                binder_add_augmentation(scope->binder, me);
            } else if (nested_ambient_module && !scope_in_script_block(scope)) {
                // In a namespace it is a member of the namespace to tsc's binder (declareSymbolAndAddToSymbolTable),
                // which no module name finds: a module of its own
                module = binder_new_symbol(scope->binder, span_sv(my name), SYMBOL_VALUE_MODULE);
                module->name_span = my name;
                module->is_module = true;
                module->module = arena_new(binder_arena(scope->binder), SymbolModule);
                module->exports = arena_new(binder_arena(scope->binder), SymbolTable);
                binder_add_module_declaration(module, me);
            }
            if (!module) {
                module = binder_ambient_module(scope->binder, cstr_sv(&my module_ref->path), my name, my attributes);
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
                .declare_keyword = span_valid(props.declare_keyword),
            });
            // (`global x`, recovery's global without a body: a shorthand ambient module, any, tsc's
            // isShorthandAmbientModuleSymbol)
            if (invalid_global && my is_shorthand && my symbol) my symbol->is_shorthand_module = true;
            my inner_scope->container = my symbol;
        }
    }
    my inner_scope->ambient = scope->ambient || props.declared;
    my inner_scope->export_context = my inner_scope->ambient && !block_has_export_declarations(&my contents);
    if (my is_ambient_module && my is_augmentation && !block_has_export_declarations(&my contents)) my inner_scope->export_context = true;

    // (A module's `declare global`, and a `global` block directly in an ambient module of a script, merge into the
    // globals after every file's own: both are module augmentations to tsc's collectModuleReferences, merged by
    // initializeTypeChecker after the files' locals. @types/node's `declare module "timers" { global { ... } }`)
    if (my is_global) {
        binder_defer_global_augmentation(scope->binder, me);
        return;
    }
    namespace_decl_add_contents(me);
}

void namespace_decl_add_contents(NamespaceDecl* me) {
    block_add_names(&my contents, my inner_scope);
    scope_check_export_merges(my inner_scope, block_span(&my contents));
}

void namespace_decl_resolve_names(NamespaceDecl* me, Scope* names, ErrorCtx* errs) {
    // The scope was made when the namespace was declared
    my inner_scope->flow = names->flow;
    my inner_scope->self = my symbol;
    // (The attributes are named from outside the module: tsgo's resolveName skips a module's exports for them)
    if (my attributes) type_lookup_names(my attributes, names, errs);
    block_lookup_names_container(&my contents, c_sv("namespace"), my name, my inner_scope, errs);
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

// The symbol of the other export status that tsc's local symbol merges with `symbol` in the block `decl` is in: an
// exported one's local, or a local's export (tsc's declareModuleMember makes one local symbol of both)
static Symbol const* same_block_counterpart(Symbol const* symbol, SymbolDecl const* decl) {
    Scope const* block = decl ? decl->block : nullptr;
    if (!block || !block->container || !block->container->exports) return nullptr;
    Symbol const* exported = symbol_table_get(block->container->exports, symbol->name);
    Symbol const* local = symbol_table_get(scope_locals(((Scope*)block)), symbol->name);
    Symbol const* other = exported == symbol ? local : local == symbol ? exported : nullptr;
    if (!other || other == symbol) return nullptr;
    for (u32 i = 0; i < other->decl_count; i++) {
        if (other->decls[i].block == block) return other;
    }
    return nullptr;
}

// tsgo's checkGrammarImportAttributesType: each member of `declare module "*.x" with { ... }` is `name: "string"`.
// Reports the first that isn't
static bool module_attributes_check_grammar(Interface const* attributes, ErrorCtx* errs) {
    for_each (i, vec_interface_member, &attributes->members) {
        Span decl = i->declaration;
        if (i->is_method || i->accessor || i->accessor_kind || (i->key.tag != INTERFACE_KEY_NAME && i->key.tag != INTERFACE_KEY_EXPR)) {
            error_ctx_grammar_error_d(errs, decl, 1552, "An import attributes type may only contain property signatures.");
            return true;
        }
        if (i->readonly) {
            Span modifier = decl;
            if (csview_starts_with(span_sv(decl), "readonly")) modifier.size = 8;
            error_ctx_grammar_error_d(errs, modifier, 1558, "An import attributes property cannot have a 'readonly' modifier.");
            return true;
        }
        if (i->implicit_any) {
            error_ctx_grammar_error_d(errs, decl, 1553, "An import attributes property must have a type annotation.");
            return true;
        }
        if (i->optional) {
            error_ctx_grammar_error_d(errs, decl, 1556, "An import attributes property cannot be optional.");
            return true;
        }
        csview key = span_sv(i->key.span);
        if (i->key.tag != INTERFACE_KEY_NAME || (key.size && (isdigit((unsigned char)*key.buf) || *key.buf == '.'))) {
            error_ctx_grammar_error_d(errs, i->key.span, 1554, "An import attributes property must have a string literal or identifier name.");
            return true;
        }
        if (cstr_equals(&i->key.name_string, "resolution-mode")) {
            error_ctx_grammar_error_d(errs, i->key.span, 1557, "'resolution-mode' is not a valid key for an import attributes type.");
            return true;
        }
        // (The annotation: after the name's `?` and `:`, to the end of the member. A string literal, not a template)
        char const* start = span_end_ptr(i->key.span);
        char const* end = span_end_ptr(decl);
        while (start < end && (isspace((unsigned char)*start) || *start == '?' || *start == ':')) start++;
        while (end > start && isspace((unsigned char)end[-1])) end--;
        if (i->type.tag != TYPE_VALUE || i->type.value.tag != VALUE_STRING || i->type.parenthesized || start == end ||
            (*start != '"' && *start != '\'')
        ) {
            error_ctx_grammar_error_d(errs, span_make_in(decl, start, end), 1555,
                "An import attributes property must have a string literal type annotation.");
            return true;
        }
    }
    return false;
}

void namespace_decl_typecheck(NamespaceDecl* me, bool declared, ErrorCtx* errs) {
    // tsgo's checkImportAttributesType, and no attributes on an augmentation (TS1551)
    if (my attributes) {
        module_attributes_check_grammar(&my attributes->interface, errs);
        type_typecheck(my attributes, errs);
        ty_check_module_attributes(current_checker, my attributes, my attributes->interface.span, errs);
        if (my is_augmentation) {
            error_ctx_error_d(errs, my attributes->interface.span, 1551, "Import attributes are not allowed on a module augmentation.");
        }
    }
    // tsc's checkModuleDeclaration: an instantiated namespace merged with a class or function comes after it, in the
    // same file
    SymbolDecl const* own = nullptr;
    for (u32 i = 0; my symbol && i < my symbol->decl_count && !own; i++) {
        if (my symbol->decls[i].node == me) own = &my symbol->decls[i];
    }
    Symbol const* counterpart = own && !declared ? same_block_counterpart(my symbol, own) : nullptr;
    // (tsc's checkModuleDeclarationDiagnostics)
    if (!declared && my is_ambient_module) {
        error_ctx_grammar_error_d(errs, my name, 1035, "Only ambient modules can use quoted names.");
    }
    if (my module_keyword && !my is_ambient_module && !my is_global && !my misplaced && !my jsdoc) {
        error_ctx_error_d(errs, my name, 1540, "A 'namespace' declaration should not be declared using the 'module' "
            "keyword. Please use the 'namespace' keyword instead.");
    }
    CompilerOptions const* options = compiler_options();
    bool isolated = options->isolated_modules || options->verbatim_module_syntax;
    if (!declared && !my is_global && (options->erasable_syntax_only || (isolated && !span_source(my full_span)->is_module)) &&
        namespace_decl_is_instantiated(me)
    ) {
        if (options->erasable_syntax_only) {
            error_ctx_error_d(errs, my name, 1294, "This syntax is not allowed when 'erasableSyntaxOnly' is enabled.");
        }
        if (isolated && !span_source(my full_span)->is_module) {
            error_ctx_error_d(errs, my name, 1280, "Namespaces are not allowed in global script files when '",
                options->verbatim_module_syntax ? "verbatimModuleSyntax" : "isolatedModules", "' is enabled. If this "
                "file is not intended to be a global script, set 'moduleDetection' to 'force' or add an empty 'export {}' "
                "statement.");
        }
    }
    if (!declared && !my is_global && !my is_ambient_module && my symbol && (my symbol->decl_count > 1 || counterpart) &&
        namespace_decl_is_instantiated(me)
    ) {
        SymbolDecl const* first = first_non_ambient_class_or_function(my symbol);
        // (Declarations of the other export status in this block are the same symbol to tsc)
        SymbolDecl const* other = counterpart ? first_non_ambient_class_or_function(counterpart) : nullptr;
        if (other && other->block == own->block && (!first || span_start_ptr(other->full) < span_start_ptr(first->full))) {
            first = other;
        }
        if (first && span_source(first->full) != span_source(my full_span)) {
            error_ctx_error_d(errs, my name, 2433,
                "A namespace declaration cannot be in a different file from a class or function with which it is merged.");
        } else if (first && span_start_ptr(my full_span) < span_start_ptr(first->full)) {
            error_ctx_error_d(errs, my name, 2434,
                "A namespace declaration cannot be located prior to a class or function with which it is merged.");
        }
    }
    // tsc's checkModuleAugmentationElement, for an augmentation that applies (global, or merged with its module)
    bool applies = (my is_global && !my is_invalid_global) ||
        (my is_augmentation && my module_ref && binder_module_of_ref(current_checker->binder, my module_ref));
    for_each (i, vec_statement, applies ? &my contents.statements : &(VecStatement){0}) {
        bool is_export = i->tag == STATEMENT_EXPORT_DEFAULT_EXPR;
        bool is_import = false;
        if (i->tag == STATEMENT_IMPORT_EXPORT) {
            PortStatement const* port = i->import_export;
            if (csview_equals(span_sv(port->keyword_span), "export")) {
                is_export = port->main.tag != PORT_IMPORT_ALIAS && port->main.tag != PORT_EXPORT_AS_NAMESPACE &&
                    !port->is_import_require && port->main.tag != PORT_IMPORT_GATHER;
                is_import = port->is_import_require;
            } else {
                // (`import a = e.x` is fine)
                is_import = port->main.tag != PORT_IMPORT_ALIAS;
            }
        }
        if (is_export) {
            error_ctx_grammar_error_d(errs, block_first_token(i->span), 2666,
                "Exports and export assignments are not permitted in module augmentations.");
        } else if (is_import) {
            error_ctx_grammar_error_d(errs, block_first_token(i->span), 2667,
                "Imports are not permitted in module augmentations. Consider moving them to the enclosing external module.");
        }
    }
    bool flow_disabled = current_checker->flow_analysis_disabled;
    block_typecheck(&my contents, (BlockTypecheckCtx){ .ambient = declared }, errs);
    current_checker->flow_analysis_disabled = flow_disabled;
}

// If statements

// `for (#x in o)` in a class: tsc's checkGrammarPrivateIdentifierExpression has nothing against that private name
static bool for_in_private_variable(ForEachLoopStatement const* me) {
    Expr const* variable = &my variable.expr;
    return my keyword == FOR_EACH_LOOP_IN && variable->tag == EXPR_PRIVATE_NAME && !expr_is_perenthisized(variable) &&
        variable->private_name->enclosing_class;
}

zsview for_each_loop_statement_keyword_str(ForEachLoopStatement const* me) {
    switch (my keyword) {
        case FOR_EACH_LOOP_OF: return c_zv("of");
        case FOR_EACH_LOOP_IN: return c_zv("in");
    }
    impossible;
}

// tsc's checkRightHandSideOfForOf: checkNonNullExpression of the iterable, the error type if nothing is left
static Ty const* for_of_iterable(Checker* checker, Expr const* iterable, Ty const* type, ErrorCtx* errs) {
    if (type->flags & TYF_ANY) return type;
    Ty const* non_null = ty_check_non_null_type(checker, type, iterable, errs);
    return non_null ? non_null : ty_intrinsic(checker, TY_ERROR);
}

Ty const* for_each_loop_statement_evaluate_var_type(ForEachLoopStatement* me) {
    Checker* checker = current_checker;
    Ty const* iterable = ty_expr_type(checker, &my iterable);
    if (iterable == ty_intrinsic(checker, TY_CIRCULAR)) return iterable; // Self-referential
    switch (my keyword) {
        case FOR_EACH_LOOP_IN:
            return ty_for_in_key_type(checker, iterable);

        case FOR_EACH_LOOP_OF:
            // (tsc's widenTypeForVariableLikeDeclaration: getWidenedType, so the elements of an array literal aren't
            // fresh object literals in the variable)
            return ty_widened_for_declaration(checker, ty_iterated_type(checker, my is_await ? ITERATION_FOR_AWAIT_OF :
                ITERATION_FOR_OF, for_of_iterable(checker, &my iterable, iterable, nullptr), expr_outer_span(&my iterable), nullptr), false);
    }
    impossible;
}

Ty const* for_each_loop_statement_pattern_source_type(ForEachLoopStatement const* me) {
    Checker* checker = current_checker;
    Ty const* var_type = lazy_type_get((LazyType*)&my var_type);
    if (my keyword != FOR_EACH_LOOP_OF || var_type == ty_intrinsic(checker, TY_CIRCULAR)) return var_type;
    return ty_iterated_type(checker, my is_await ? ITERATION_FOR_AWAIT_OF : ITERATION_FOR_OF,
        for_of_iterable(checker, &my iterable, ty_expr_type(checker, (Expr*)&my iterable), nullptr), expr_outer_span(&my iterable), nullptr);
}

// What `for (k in x)` takes: object types and type parameters (every member of a union, some member of an
// intersection: `T & object` is what `typeof t === "object"` makes of a T). Without strict null checks also null
static bool for_in_accepts(Checker* checker, Ty const* ty) {
    // (tsc's isTypeAssignableToKind(NonPrimitive | InstantiableNonPrimitive): type variables, conditionals and
    // substitutions outright, not `keyof T`, template literals and string mappings, InstantiablePrimitive)
    if (ty->kind == TY_KEYOF || ty->kind == TY_TEMPLATE_LITERAL || ty->kind == TY_STRING_MAPPING) return false;
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
    if (statement_declares_name(my tag) && my decl.props.invalid_decorators) {
        vec_expr_drop(my decl.props.invalid_decorators);
        ast_delete(my decl.props.invalid_decorators);
        my decl.props.invalid_decorators = nullptr;
    }
    switch (my tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_SOURCE_DIRECTIVE:
            cstr_drop(&my source_directive->path);
            cstr_drop(&my source_directive->not_found);
            cstr_drop(&my source_directive->unbuilt_output);
            cstr_drop(&my source_directive->unbuilt_source);
            ast_delete(my source_directive);
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
            ast_delete(my for_loop);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT: variable_statement_drop(&my for_each_loop->variable.statement); break;
                case FOR_EACH_LOOP_EXPR: expr_drop(&my for_each_loop->variable.expr); break;
            }
            lazy_type_drop(&my for_each_loop->var_type);
            expr_drop(&my for_each_loop->iterable);
            statement_drop(&my for_each_loop->body);
            ast_delete(my for_each_loop);
            break;

        case STATEMENT_SWITCH:
            expr_drop(&my switch_->needle);
            vec_switch_case_drop(&my switch_->cases);
            ast_delete(my switch_);
            break;

        case STATEMENT_TRY_CATCH:
            block_drop(&my try_catch->try_block);
            binding_drop(&my try_catch->catch_var);
            if (my try_catch->catch_initializer) {
                expr_drop(my try_catch->catch_initializer);
                ast_delete(my try_catch->catch_initializer);
            }
            block_drop(&my try_catch->catch_block);
            block_drop(&my try_catch->finally_block);
            ast_delete(my try_catch);
            break;

        case STATEMENT_LABELED:
            statement_drop(my labeled.body);
            ast_delete(my labeled.body);
            break;

        case STATEMENT_WITH:
            expr_drop(&my with->object);
            statement_drop(&my with->body);
            ast_delete(my with);
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
                ast_delete(my expr_jsdoc_type);
            }
            break;

        case STATEMENT_IMPORT_EXPORT:
            port_statement_drop(my import_export);
            ast_delete(my import_export);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_drop(&my decl.variable);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_drop(my decl.function.impl);
            ast_delete(my decl.function.impl);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_drop(my decl.enum_);
            ast_delete(my decl.enum_);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_drop(my decl.class);
            ast_delete(my decl.class);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_drop(my decl.type);
            ast_delete(my decl.type);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_drop(my decl.interface);
            ast_delete(my decl.interface);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_drop(my decl.namespace);
            ast_delete(my decl.namespace);
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
            span = span_make_in(span, span_start_ptr(span), span_start_ptr(span) + 1);
        }
        error_ctx_error_d(
            binder_errs(scope->binder),
            span,
            my keyword == FOR_EACH_LOOP_OF ? 1188 : 1091,
            "Only a single variable declaration is allowed in a 'for...",
            for_each_loop_statement_keyword_str(me).str, "' statement."
        );
    } else if (var_size == 0) {
        // Error added by parser
    } else {
        InitializedBinding* initializer = initializers->data;
        if (initialized_binding_value(initializer)->tag != EXPR_IMPLICIT_ANY) {
            error_ctx_error_d(
                binder_errs(scope->binder),
                initializer->binding.content_span,
                my keyword == FOR_EACH_LOOP_OF ? 1190 : 1189,
                "The variable declaration of a 'for...",
                for_each_loop_statement_keyword_str(me).str,
                "' statement cannot have an initializer."
            );
        } else if (initializer->binding.has_explicit_type) {
            error_ctx_error_d(
                binder_errs(scope->binder),
                initializer->binding.content_span,
                my keyword == FOR_EACH_LOOP_OF ? 2483 : 2404,
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
            // (`for (const x of xs)` declares constant variables like any `const`: tsc's isConstantVariable)
            .is_const = variable_statement_is_const(&my variable.statement),
        });
    }
}

// A block (or blocks) directly in a script, no function or namespace between
static bool scope_in_script_block(Scope const* scope) {
    while (scope && scope->kind == SCOPE_BLOCK) scope = scope->parent;
    return scope && scope->kind == SCOPE_SCRIPT;
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
        case STATEMENT_DECL_NAMESPACE: {
            // `declare module "m"` in a block of a script (TS2435) is a global ambient module all the same: a block is
            // no container to tsc's binder, the file is. Known before linking, so that imports find it; the block
            // declares into the same module symbol when its turn comes
            NamespaceDecl const* ns = my decl.namespace;
            if (scope->kind == SCOPE_SCRIPT && ns->is_ambient_module && !ns->is_global && ns->module_ref) {
                binder_ambient_module(scope->binder, cstr_sv(&ns->module_ref->path), ns->name, ns->attributes);
            }
        }   break;

        case STATEMENT_DECL_FUNCTION:
            // (`export function` in a block of a namespace is exported: tsc's declareSymbolAndAddToSymbolTable goes by the
            // container, declareModuleMember)
            if (!scope->strict) {
                DeclStatementProps props = my decl.props;
                if (props.exported != DECL_EXPORTED_YES) props.exported = DECL_EXPORTED_NO;
                function_impl_add_names(my decl.function.impl, scope, props, true);
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
                variable_statement_add_names(&my decl.variable, scope, my decl.props);
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

// An `if` branch whose names statement_add_names() of the `if` declares (in the scope around)
static bool branch_hoisted(Statement const* branch) {
    return branch->tag == STATEMENT_DECL_TYPE || branch->tag == STATEMENT_DECL_INTERFACE || branch->tag == STATEMENT_IF_ELSE;
}

void statement_add_names(Statement* me, Scope* scope) {
    switch (my tag) {
        case STATEMENT_EXPR: // (CommonJS exports: scope_add_commonjs_module())
            break;

        case STATEMENT_EXPORT_DEFAULT_EXPR:
            my export_misplaced = !scope_export_default_expr(scope, &my expr, my expr_jsdoc_type, my span, my export_equals);
            break;

        case STATEMENT_IMPORT_EXPORT:
            port_statement_add_names(my import_export, scope);
            break;

        case STATEMENT_DECL_VARIABLE:
            // The `var`s of nested blocks were declared by statement_hoist_names()
            if (my decl.variable.keyword == VAR_INIT_VAR && !scope_is_var_container(scope)) break;
            variable_statement_add_names(&my decl.variable, scope, my decl.props);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_add_names(my decl.function.impl, scope, my decl.props, scope->kind == SCOPE_BLOCK && !scope->strict);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_add_names(my decl.enum_, scope, my decl.props);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_add_names(my decl.class, scope, my decl.props);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_add_names(my decl.type, scope, my decl.props);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_add_names(my decl.interface, scope, my decl.props);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_add_names(my decl.namespace, scope, my decl.props);
            break;

        case STATEMENT_LABELED:
            if (my labeled.body->tag == STATEMENT_DECL_NAMESPACE) my labeled.body->decl.namespace->labeled = true;
            statement_add_names(my labeled.body, scope);
            break;

        case STATEMENT_IF_ELSE:
            // A type alias or interface as a branch (TS1156) belongs to the scope around, declared up front like the
            // others there (tsc's binder): `"" as s; if (a) type s = string;` finds it
            for_each (i, vec_statement, &my if_else.statements) {
                if (branch_hoisted(i)) statement_add_names(i, scope);
            }
            break;

        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
        case STATEMENT_BLOCK:
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
            return namespace_decl_is_instantiated(my decl.namespace);
        case STATEMENT_DECL_ENUM:
            return !my decl.enum_->is_const || compiler_options()->preserve_const_enums;
        case STATEMENT_DECL_VARIABLE:
            if (my decl.variable.keyword != VAR_INIT_VAR) return true;
            for_each (i, vec_initialized_binding, &my decl.variable.initializers) {
                if (initialized_binding_value(i)->tag == EXPR_IMPLICIT_ANY) return false; // `var x;` may declare a variable used above
            }
            return true;
        default:
            return true;
    }
}

// Whether the statement is unreachable. The first statement of unreachable code is where TS7027 goes, which is an
// error only when allowUnreachableCode is explicitly false (unset: a suggestion, which baselines don't have)
static bool statement_check_unreachable(Statement const* me, FlowBuilder* flow, VecStatement const* list, bool* in_range) {
    if (!flow_is_unreachable(flow->current)) return false;
    // (After a reported statement everything executable is in a reported range)
    *in_range = statement_is_executable(me) && (flow->current == &flow_reported_unreachable || flow->within_unreachable);
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
    *in_range = statement_is_executable(me);

    CompilerOptions const* options = compiler_options();
    bool is_error = options->is_set[COMPILER_OPTION_allow_unreachable_code] && !options->allow_unreachable_code &&
        !flow->ambient && statement_is_executable(me) && !flow->within_unreachable;
    if (!is_error) return true;
    if (!list || !statement_is_executable(me)) {
        error_ctx_error_d(flow->errs, my span, 7027, "Unreachable code detected.");
        return true;
    }
    // In a list of statements: every run of executable statements from here on (tsc's checkSourceElementUnreachable
    // reports a run, and each later statement it then visits starts the next)
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

// The scope of an `if` branch: a statement that declares nothing where it is (a block, loop, `switch`, `try` makes
// scopes of its own; `return`, `break`...) needs none, the rest a block scope (`if (a) function f() {}`, `let`)
static Scope* branch_names_scope(Statement const* branch, Scope* names) {
    switch (branch->tag) {
        case STATEMENT_NOOP:
        case STATEMENT_BLOCK:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
        // (A type alias or interface goes to tsc's blockScopeContainer, the scope around: TS1156 says it's misplaced)
        case STATEMENT_DECL_TYPE:
        case STATEMENT_DECL_INTERFACE:
            return names;
        case STATEMENT_DECL_VARIABLE:
            // `if (a) const x = 1;` (TS1156) declares x where the `if` is: an `if` isn't a block scope container to tsc's
            // binder
            return names;
        default: {
            Scope* scope = scope_new(names, SCOPE_BLOCK);
            scope->binder_transparent = true;
            return scope;
        }
    }
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
        Scope* branch_scope = branch_names_scope(branch, names);
        if (!branch_hoisted(branch)) statement_add_names(branch, branch_scope);
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
        Scope* branch_scope = branch_names_scope(branch, names);
        if (!branch_hoisted(branch)) statement_add_names(branch, branch_scope);
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
            flow->statement_list = &clause->statements;
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
        if (my catch_initializer) expr_lookup_names(my catch_initializer, catch_scope, errs);
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
// tsc's checkGrammarStatementInAmbientContext reports TS1036 at the first statement of a block an ambient context
// doesn't allow, and that statement's own checks stop there (checkReturnStatement)
static bool ambient_reported_at(Statement const* me, Scope const* names) {
    VecStatement const* list = names->ambient && names->flow ? names->flow->lookup_statements : nullptr;
    if (!list || !span_source(me->span) || span_source(me->span)->has_parse_errors) return false;
    for_each (i, vec_statement, list) {
        if (block_statement_is_ambient_disallowed(i)) return i == me;
    }
    return false;
}

static void statement_resolve_names_reachable(Statement* me, u32 label_count, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    if (statement_declares_name(my tag) || my tag == STATEMENT_IMPORT_EXPORT) {
        unused_register(names, UNUSED_STATEMENT, me);
    }
    switch (my tag) {
        case STATEMENT_LABELED: {
            for (FlowActiveLabel* i = flow->active_labels; i; i = i->next) {
                if (!name_equals(span_sv(i->name), span_sv(my labeled.name))) continue;
                error_ctx_grammar_error_d(errs, my labeled.name, 1114, "Duplicate label '", span_sv(my labeled.name), "'.");
                break;
            }
            // tsc's binder (checkStrictModeLabeledStatement): no label on a declaration in strict mode code from ES2015 on
            Statement const* body = my labeled.body;
            if (names->strict && (statement_declares_name(body->tag) || body->tag == STATEMENT_IMPORT_EXPORT)
            ) {
                error_ctx_error_d(errs, my labeled.name, 1344, "A label is not allowed here.");
            }
            // (A type alias under a label isn't visible: tsc's isDeclarationVisible, the container is the label)
            if (body->tag == STATEMENT_DECL_TYPE) body->decl.type->local = true;
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
            Span keyword = span_make_in(my span, span_start_ptr(my span), span_start_ptr(my span) + 4);
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
                expr_mark_result_unused(&loop->afterthought);
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
            // An initializer that ends the flow (a throwing IIFE): the rest stays unreachable. Entering the loop
            // label would leave it only the back edge, a cycle with no way in (tsgo's bindForStatement bails out)
            if (!flow_is_unreachable(flow->current)) flow_set_current(flow, pre_loop);
            expr_lookup_names_condition(&loop->condition, loop_scope, errs, pre_body, post);
            flow_set_current(flow, flow_finish_label(pre_body));
            // (tsgo's bindWhileStatement: `continue` is another way back into the loop label, no incrementor between)
            statement_resolve_names_body(&loop->body, loop_scope, errs, post, loop->is_while ? pre_loop : pre_incrementor);
            flow_add_antecedent(flow, pre_incrementor, flow->current);
            flow_set_current(flow, flow_finish_label(pre_incrementor));
            expr_mark_result_unused(&loop->afterthought);
            expr_lookup_names(&loop->afterthought, loop_scope, errs);
            flow_add_antecedent(flow, pre_loop, flow->current);
            flow_set_current(flow, flow_finish_label(post));
        }   break;

        case STATEMENT_FOR_EACH_LOOP: {
            ForEachLoopStatement* loop = my for_each_loop;
            await_check_grammar(loop->await_place, AWAIT_KIND_FOR, loop->await_span, scope_containing_function(names), errs);
            if (loop->variable.tag == FOR_EACH_LOOP_STATEMENT) variable_statement_check_await_place(&loop->variable.statement, names, errs);
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
            expr_lookup_names(&loop->iterable, loop_scope, for_of_checks_iterable(loop) ? errs : nullptr);
            flow->initializing = outer_initializing;
            flow_add_antecedent(flow, pre_loop, flow->current);
            // (An iterable that ends the flow: unreachable from here, as in the `for` loop above)
            if (!flow_is_unreachable(flow->current)) flow_set_current(flow, pre_loop);
            flow_add_antecedent(flow, post, flow->current);
            if (is_declaration) {
                unused_register(loop_scope, UNUSED_FOR_IN_OF_VARIABLES, &loop->variable.statement);
                for_each (i, vec_initialized_binding, &loop->variable.statement.initializers) {
                    // (What its pattern's computed names and defaults may not use yet)
                    flow->initializing = i;
                    binding_lookup_names(&i->binding, loop_scope, errs);
                    flow->initializing = outer_initializing;
                    if (i->value) expr_lookup_names(i->value, loop_scope, errs);
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
            if (loop->keyword == FOR_EACH_LOOP_IN) scope_rare_mut(loop_scope)->for_in_body = loop; // (Its body from here on)
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
            break;

        case STATEMENT_EXPORT_DEFAULT_EXPR: {
            // tsc's checkExportAssignment: neither belongs in a namespace. (tsgo checks the expression first, wherever
            // the statement is)
            bool in_namespace = !my export_misplaced && names->kind == SCOPE_NAMESPACE &&
                !(names->container && (names->container->is_module || names->container->is_augmentation));
            if (in_namespace) {
                if (my export_equals) {
                    error_ctx_error_d(errs, span_with_semicolon(my span), 1063, "An export assignment cannot be used in a namespace.");
                } else {
                    error_ctx_error_d(errs, span_with_semicolon(my span), 1319, "A default export can only be used in an "
                        "ECMAScript-style module.");
                }
            }
            // `export = x` doesn't use x before its declaration: the emit moves it (tsc)
            if (!my export_misplaced && !in_namespace) export_assignment_check_isolated(me, names, errs);
            bool saved_type_query = flow->in_type_query;
            if (my export_equals && my expr.tag == EXPR_IDENTIFIER) flow->in_type_query = flow->export_equals_name = true;
            flow->type_only_alias_ok = my expr.tag == EXPR_IDENTIFIER;
            expr_lookup_names(&my expr, names, errs);
            flow->in_type_query = saved_type_query;
            flow->export_equals_name = false;
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
            } else if (!container && !ambient_reported_at(me, names)) {
                // (grammarErrorOnFirstToken: the `return`, not reported in a file with parse errors)
                error_ctx_grammar_error_d(errs, block_first_token(my span), 1108,
                    "A 'return' statement can only be used within a function body.");
            }
            // (tsgo's checkReturnStatement checks the expression first, wherever the `return` is)
            expr_lookup_names(&my expr, names, errs);
            // (unreachable or not: tsc's checkAndAggregateReturnExpressionTypes goes by forEachReturnStatement)
            if (statement_is_bare_return(me)) flow->has_bare_return = true;
            if (!flow->dead) {
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
                    if (!name_equals(span_sv(i->name), span_sv(my jump_label))) continue;
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

        case STATEMENT_DECL_FUNCTION: {
            ScopePendingMark pending_mark = scope_pending_mark(names->binder);
            Scope* self_scope = scope_new_self(names, my decl.function.impl->symbol, my decl.props.declared);
            function_impl_lookup_names(my decl.function.impl, true, self_scope, errs);
            if (self_scope != names) scope_release(self_scope, pending_mark);
        }   break;

        case STATEMENT_DECL_ENUM:
            enum_decl_lookup_names(my decl.enum_, names, errs);
            break;

        case STATEMENT_DECL_CLASS: {
            ScopePendingMark pending_mark = scope_pending_mark(names->binder);
            Scope* self_scope = scope_new_self(names, my decl.class->symbol, my decl.props.declared);
            class_decl_lookup_names(my decl.class, self_scope, errs);
            if (self_scope != names) scope_release(self_scope, pending_mark);
        }   break;

        case STATEMENT_DECL_TYPE: {
            if (my decl.type->jsdoc_check_only) {
                type_lookup_names(&my decl.type->type, names, errs);
                break;
            }
            // (A class member's typedef sees the class's type parameters)
            ScopePendingMark pending_mark = scope_pending_mark(names->binder);
            Scope* self_scope = scope_new_self(names, my decl.type->symbol, my decl.props.declared);
            // (A JSDoc `@typedef`'s `@template`s too: tsgo's reparsed TypeAliasDeclaration)
            unused_register(self_scope, UNUSED_TYPE_ALIAS, my decl.type);
            my decl.type->outer_generics = names->generics;
            my decl.type->local |= names->kind == SCOPE_BLOCK || names->kind == SCOPE_FUNCTION || names->kind == SCOPE_STATIC_BLOCK;
            Scope* generic_scope = scope_new_generic(self_scope, &my decl.type->generic, my decl.type->name, span_null, nullptr, errs);
            TypeNode* body = &my decl.type->type;
            if (body->tag == TYPE_IDENTIFIER && !body->parenthesized && !body->identifier->keyword &&
                span_equals(body->identifier->name, "intrinsic") && !identifier_type_namespace_count(body->identifier) &&
                vec_spanned_type_is_empty(&body->identifier->generic_params) && !body->identifier->jsdoc
            ) {
                body->identifier->intrinsic_keyword = true; // (tsc's parseTypeAliasDeclaration)
                body->identifier->target = body->identifier->symbol = symbol_table_get(&names->binder->globals, c_sv("#intrinsic"));
            } else {
                type_lookup_names(body, generic_scope, errs);
            }
            // (Kept under a generic scope: that one declares the type parameters)
            if (self_scope != names) scope_release(self_scope, pending_mark);
        }   break;

        case STATEMENT_DECL_INTERFACE: {
            ScopePendingMark pending_mark = scope_pending_mark(names->binder);
            Scope* self_scope = scope_new_self(names, my decl.interface->symbol, my decl.props.declared);
            interface_decl_lookup_names(my decl.interface, self_scope, errs);
            if (self_scope != names) scope_release(self_scope, pending_mark);
        }   break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_resolve_names(my decl.namespace, names, errs);
            break;

        case STATEMENT_SOURCE_DIRECTIVE:
            if (my source_directive->self_reference) {
                Span span = my source_directive->path_span;
                if (span.size >= 2 && (*span.buf == '"' || *span.buf == '\'')) {
                    span_set_sv(&span, csview_slice(span_sv(span), 1, span.size - 1));
                }
                error_ctx_error_d(errs, span, 1006, "A file cannot have a reference to itself.");
            }
            if (cstr_size(&my source_directive->unbuilt_output)) {
                Span span = my source_directive->path_span;
                if (span.size >= 2 && (*span.buf == '"' || *span.buf == '\'')) {
                    span_set_sv(&span, csview_slice(span_sv(span), 1, span.size - 1));
                }
                error_ctx_error_d(errs, span, 6305, "Output file '", my source_directive->unbuilt_output,
                    "' has not been built from source file '", my source_directive->unbuilt_source, "'.");
            }
            if (cstr_size(&my source_directive->not_found)) {
                // Without the quotes
                Span span = my source_directive->path_span;
                if (span.size >= 2 && (*span.buf == '"' || *span.buf == '\'')) {
                    span_set_sv(&span, csview_slice(span_sv(span), 1, span.size - 1));
                }
                if (my source_directive->tag == SOURCE_DIRECTIVE_REFERENCE_TYPES) {
                    error_ctx_error_d(errs, span, 2688,
                        "Cannot find type definition file for '", my source_directive->not_found, "'.");
                } else {
                    // (The reference as written, slashes normalized: tsgo's getSourceFileFromReference)
                    cstr path = cstr_clone(my source_directive->not_found);
                    cstr_replace(&path, "\\", "/");
                    if (my source_directive->not_found_extensionless) {
                        // (tsc's getSourceFileFromReferenceWorker lists every supported extension)
                        error_ctx_error_d(errs, span, 6231, "Could not resolve the path '", path,
                            "' with the extensions: ", compiler_options()->allow_js ?
                                "'.ts', '.tsx', '.d.ts', '.js', '.jsx', '.cts', '.d.cts', '.cjs', '.mts', '.d.mts', '.mjs'." :
                                "'.ts', '.tsx', '.d.ts', '.cts', '.d.cts', '.mts', '.d.mts'.");
                    } else {
                        error_ctx_error_d(errs, span, 6053, "File '", path, "' not found.");
                    }
                    cstr_drop(&path);
                }
            }
            break;

        case STATEMENT_NOOP:
            break;
    }
}

void statement_resolve_names(Statement* me, Scope* names, ErrorCtx* errs) {
    if (statement_declares_name(my tag) && my decl.props.invalid_decorators) {
        for_each (i, vec_expr, my decl.props.invalid_decorators) expr_lookup_names(i, names, nullptr);
    }
    statement_resolve_names_labeled(me, 0, names, errs);
}

static void statement_resolve_names_labeled(Statement* me, u32 label_count, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    VecStatement const* list = flow->statement_list;
    flow->statement_list = nullptr;
    bool in_range = false;
    if (statement_check_unreachable(me, flow, list, &in_range)) {
        // Like tsc: nothing in unreachable code is part of the flow graph
        bool was_dead = flow->dead;
        flow->dead = true;
        // (In a range of unreachable code: nothing in it is reported again)
        bool was_within = flow->within_unreachable;
        if (in_range) my unreachable = flow->within_unreachable = true;
        statement_resolve_names_reachable(me, label_count, names, errs);
        flow->dead = was_dead;
        flow->within_unreachable = was_within;
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
            Ty const* type = ty_check_expr(current_checker, &my expr, current_checker->return_context);
            // In a const context (the contextual return type is a `const` type parameter) it keeps its literals (tsc 6.0's
            // checkAndAggregateReturnExpressionTypes, getReturnTypeFromBody)
            if (current_checker->return_context && ty_is_const_context_kind(&my expr) &&
                ty_is_const_type_variable(current_checker, current_checker->return_context, 0)
            ) {
                type = ty_regular(current_checker, type);
            }
            ty_buf_push(out, type);
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
            if (!name_equals(span_sv(i->name), span_sv(my jump_label))) continue;
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
// Whether a returned expression (each branch of a conditional one) awaits a thenable that's no valid promise
static bool returns_thenable(Expr const* expr) {
    if (expr->tag == EXPR_TERNARY) return returns_thenable(&expr->ternary->yes) || returns_thenable(&expr->ternary->no);
    return ty_awaits_thenable(current_checker, ty_expr_type(current_checker, expr));
}

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
    Span span = expr_error_span_skip_parens(effective); // (tsc's getErrorSpanForNode: a class expression's `class`)
    if (!in_conditional && span_valid(statement->span)) {
        span = span_make_in(statement->span, span_start_ptr(statement->span), span_start_ptr(statement->span) + 6);
    }
    Ty const* source = ty_expr_type(checker, expr);
    if (awaited) {
        // (HACK: see ty_awaited_returned(). An error names the awaited type as tsc's checkAwaitedType makes it,
        // `Awaited<T>` and all)
        Ty const* unwrapped = ty_awaited_returned(checker, source);
        if (unwrapped == source || ty_is_related(checker, unwrapped, target, TY_RELATION_ASSIGNABLE)) {
            source = unwrapped;
        } else {
            source = ty_awaited(checker, source);
        }
    }
    return ty_check_assignable_expr(checker, source, target, span, effective, errs);
}

static FunctionImpl const* function_declaration(Statement const* me) {
    return me && my tag == STATEMENT_DECL_FUNCTION && !my decl.props.declared ? my decl.function.impl : nullptr;
}

void statements_check_overloads(VecStatement const* statements, ErrorCtx* errs) {
    isize count = vec_statement_size(statements);
    for (isize i = 0; i < count; i++) {
        FunctionImpl const* overload = function_declaration(vec_statement_at(statements, i));
        if (!overload || !span_valid(overload->name) || !overload->name.size) continue;
        // A declaration file is ambient
        SourceFile const* file = span_source(overload->name);
        if (file->is_declaration) return;
        FunctionImpl const* next = i + 1 < count ? function_declaration(vec_statement_at(statements, i + 1)) : nullptr;
        // An ambient declaration next starts over (tsc's checkFunctionOrConstructorSymbol forgets previousDeclaration
        // at one)
        Statement const* after_statement = i + 1 < count ? vec_statement_at(statements, i + 1) : nullptr;
        if (!next && after_statement && after_statement->tag == STATEMENT_DECL_FUNCTION && after_statement->decl.props.declared &&
            name_equals(span_sv(after_statement->decl.function.impl->name), span_sv(overload->name))
        ) {
            continue;
        }
        // The next declaration of the function in this list (an implementation followed by an overload after an empty
        // statement, `function f() {};`, counts too)
        isize next_at = i + 1;
        // (Only for a function declared more than once: the scan is quadratic in a block of many functions)
        if (!(next && name_equals(span_sv(next->name), span_sv(overload->name))) && (overload->has_body || overload->body_missing) &&
            overload->symbol && overload->symbol->decl_count > 1
        ) {
            for (isize j = i + 1; j < count; j++) {
                FunctionImpl const* other = function_declaration(vec_statement_at(statements, j));
                if (other && name_equals(span_sv(other->name), span_sv(overload->name))) {
                    next = other;
                    next_at = j;
                    break;
                }
            }
        }
        if (next && name_equals(span_sv(next->name), span_sv(overload->name))) {
            // A declaration the next one of the function doesn't directly follow (a token was skipped in between) is
            // missing its implementation, body or not, unless the next is a second implementation (tsc's
            // checkFunctionOrConstructorSymbol: previousDeclaration.end !== node.pos)
            Statement const* here = vec_statement_at(statements, i);
            Statement const* after = vec_statement_at(statements, next_at);
            bool adjacent = next_at == i + 1 && span_only_trivia_between(span_end_ptr(here->span), span_start_ptr(after->span),
                !overload->has_body && !overload->body_missing);
            bool some_body = false;
            for (isize j = 0; j <= i && next->has_body && !some_body; j++) {
                FunctionImpl const* other = function_declaration(vec_statement_at(statements, j));
                some_body = other && other->has_body && name_equals(span_sv(other->name), span_sv(overload->name));
            }
            if (!adjacent && !(next->has_body && some_body)) {
                error_ctx_error_d(errs, overload->name, 2391,
                    "Function implementation is missing or not immediately following the declaration.");
                continue;
            }
        }
        if (overload->has_body || overload->body_missing) continue;
        if (next && name_equals(span_sv(next->name), span_sv(overload->name))) continue; // It goes on
        // Not followed by its implementation. An error if the list declares more of it later (tsc's consecutive
        // declarations), or if nothing declares more of it (the last declaration has no body)
        bool later_here = false;
        for (isize j = i + 1; j < count && !later_here; j++) {
            FunctionImpl const* other = function_declaration(vec_statement_at(statements, j));
            later_here = other && name_equals(span_sv(other->name), span_sv(overload->name));
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

// The declarations of tsc's local symbol of a function: those of `symbol` and, in a module or namespace, those the same
// block exports under that name (tttt keeps the exported ones in a symbol of their own), in source order. `exported`
// says which are exported
static u32 function_local_symbol_decls(Symbol const* symbol, SymbolDecl const** out, bool* exported, u32 cap) {
    Scope const* block = nullptr;
    for (u32 i = 0; i < symbol->decl_count && !block; i++) {
        if (symbol->decls[i].kind == SYMBOL_DECL_FUNCTION) block = symbol->decls[i].block;
    }
    Symbol const* local = symbol;
    Symbol const* exports = nullptr;
    if (block && (block->kind == SCOPE_MODULE || block->kind == SCOPE_NAMESPACE) && block->container && block->container->exports) {
        exports = symbol_table_get(block->container->exports, symbol->name);
        local = symbol_table_get(scope_locals(block), symbol->name);
    }
    if (exports == local) exports = nullptr;
    u32 count = 0;
    Symbol const* symbols[2] = { local, exports };
    for (u32 s = 0; s < 2; s++) {
        for (u32 i = 0; symbols[s] && i < symbols[s]->decl_count && count < cap; i++) {
            SymbolDecl const* decl = &symbols[s]->decls[i];
            if (s == 1 && decl->block != block) continue;
            // (The local has the exported declarations of its block too, see scope_init_declaration(): counted once,
            // as exported)
            bool also_exported = false;
            for (u32 k = 0; s == 0 && exports && k < exports->decl_count && !also_exported; k++) also_exported = exports->decls[k].node == decl->node;
            if (also_exported) continue;
            // (insertion into source order)
            u32 at = count;
            while (at > 0 && span_source(out[at - 1]->full) == span_source(decl->full) &&
                span_start_ptr(out[at - 1]->full) > span_start_ptr(decl->full)
            ) {
                out[at] = out[at - 1];
                exported[at] = exported[at - 1];
                at--;
            }
            out[at] = decl;
            exported[at] = s == 1 || (symbols[s] == exports) || (block && block->export_context);
            count++;
        }
    }
    return count;
}

// tsc's checkFlagAgreementBetweenOverloads for the export and ambient flags (TS2383, TS2384) of a function's symbol
// with overloads: when some but not all of its function declarations have one, those that differ from the canonical
// one of their file (the implementation if it's in the same container as the file's first declaration, else that first
// declaration). `skip`: declarations reported already (tsc deduplicates diagnostics), `reported` gets those reported.
// (The class method ones, TS2385/TS2512/TS2386, are class_check_overload_agreement())
static u32 function_check_flag_agreement(
    SymbolDecl const* const* decls, bool const* exported, u32 count,
    SymbolDecl const* const* skip, u32 skip_count, SymbolDecl const** reported, ErrorCtx* errs
) {
    bool some_ambient = false, all_ambient = true, some_exported = false, all_exported = true, overloads = false;
    SymbolDecl const* implementation_decl = nullptr;
    for (u32 i = 0; i < count; i++) {
        if (decls[i]->kind != SYMBOL_DECL_FUNCTION) continue;
        if (!implementation_decl && ((FunctionImpl const*)decls[i]->node)->has_body) implementation_decl = decls[i];
        overloads |= !((FunctionImpl const*)decls[i]->node)->has_body;
        bool ambient = function_symbol_decl_is_ambient(decls[i]);
        some_ambient |= ambient;
        all_ambient &= ambient;
        some_exported |= exported[i];
        all_exported &= exported[i];
    }
    if (!overloads || (some_ambient == all_ambient && some_exported == all_exported)) return 0;
    u32 reported_count = 0;
    for (u32 i = 0; i < count; i++) {
        SymbolDecl const* decl = decls[i];
        bool skipped = false;
        for (u32 j = 0; j < skip_count; j++) skipped |= skip[j] == decl;
        if (skipped) continue;
        SourceFile const* file = span_source(decl->name) ? span_source(decl->name) : span_source(decl->full);
        // The canonical declaration of its file
        u32 file_first = 0;
        while ((span_source(decls[file_first]->name) ? span_source(decls[file_first]->name) : span_source(decls[file_first]->full)) != file) file_first++;
        u32 canonical = file_first;
        for (u32 j = 0; implementation_decl && j < count; j++) {
            if (decls[j] == implementation_decl && implementation_decl->block == decls[file_first]->block) canonical = j;
        }
        if (some_exported != all_exported && exported[i] != exported[canonical]) {
            error_ctx_error_d(errs, symbol_decl_error_span(decl), 2383, "Overload signatures must all be exported or non-exported.");
            reported[reported_count++] = decl;
        } else if (function_symbol_decl_is_ambient(decl) != function_symbol_decl_is_ambient(decls[canonical])) {
            error_ctx_error_d(errs, symbol_decl_error_span(decl), 2384, "Overload signatures must all be ambient or non-ambient.");
            reported[reported_count++] = decl;
        }
    }
    return reported_count;
}

// tsc's checkFunctionOrMethodDeclaration checks the flags of the function's local symbol once, and those of its export
// symbol (across the blocks of a namespace) too
static void function_check_flag_agreements(FunctionImpl const* me, Symbol const* symbol, ErrorCtx* errs) {
    // (The common case, a lone implementation: only a declaration in the block's other table could disagree with it)
    if (symbol->decl_count == 1 && my has_body) {
        Scope const* block = symbol->decls[0].block;
        if (!block || (block->kind != SCOPE_MODULE && block->kind != SCOPE_NAMESPACE) || !block->container ||
            !block->container->exports
        ) {
            return;
        }
        Symbol const* other = symbol->parent == block->container ? symbol_table_get(scope_locals(block), symbol->name) :
            symbol_table_get(block->container->exports, symbol->name);
        if (!other || other == symbol) return;
    }
    SymbolDecl const* decls[64];
    bool exported[64];
    SymbolDecl const* reported[64];
    u32 reported_count = 0;
    u32 count = function_local_symbol_decls(symbol, decls, exported, c_arraylen(decls));
    // Once per local symbol, from its first function declaration
    for (u32 i = 0; i < count; i++) {
        if (decls[i]->kind != SYMBOL_DECL_FUNCTION) continue;
        if (decls[i]->node == me) {
            reported_count = function_check_flag_agreement(decls, exported, count, nullptr, 0, reported, errs);
        }
        break;
    }
    // The export symbol, once (from its first function declaration)
    Scope const* block = nullptr;
    for (u32 i = 0; i < symbol->decl_count && !block; i++) {
        if (symbol->decls[i].kind == SYMBOL_DECL_FUNCTION) block = symbol->decls[i].block;
    }
    if (!block || !block->container || !block->container->exports || symbol_table_get(block->container->exports, symbol->name) != symbol) return;
    if (symbol_find_node(symbol, SYMBOL_DECL_FUNCTION) != me) return;
    count = 0;
    for (u32 i = 0; i < symbol->decl_count && count < c_arraylen(decls); i++) {
        decls[count] = &symbol->decls[i];
        exported[count++] = true;
    }
    SymbolDecl const* more[64];
    function_check_flag_agreement(decls, exported, count, reported, reported_count, more, errs);
}

// tsc's duplicateFunctionDeclaration: more than one declaration of the function has a body, reported on all of them.
// And that the overloads are compatible with the implementation (TS2394)
static void function_check_duplicate_implementation(FunctionImpl const* me, ErrorCtx* errs) {
    Symbol const* symbol = my symbol;
    if (!symbol) return;
    // Once per symbol, from its first declaration that checks it. A JavaScript file may redeclare a function: its
    // declarations only check an exported symbol (tsc's checkFunctionOrMethodDeclaration skips the local symbol of a
    // NodeFlagsJavaScriptFile declaration)
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_FUNCTION) continue;
        FunctionImpl const* function = symbol->decls[i].node;
        SourceFile const* file = span_source(function->full_span);
        if (!symbol->parent && file && file->is_js) continue;
        if (function != me) return;
        break;
    }
    if (!symbol->parent && span_source(my full_span) && span_source(my full_span)->is_js) return;
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
    function_check_flag_agreements(me, symbol, errs);
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

static bool binding_declares_name(Binding const* me, csview name) {
    switch (my tag) {
        case BINDING_NOOP: return false;
        case BINDING_IDENTIFIER: return csview_equals_sv(span_sv(my content_span), name);
        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) if (binding_declares_name(i, name)) return true;
            return false;
        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) if (binding_declares_name(&i->target, name)) return true;
            return false;
    }
    return false;
}

static void catch_check_block_names(Binding const* catch_var, Binding const* declared, ErrorCtx* errs) {
    switch (declared->tag) {
        case BINDING_NOOP: break;
        case BINDING_IDENTIFIER:
            if (binding_declares_name(catch_var, span_sv(declared->content_span))) {
                error_ctx_grammar_error_d(errs, declared->content_span, 2492, "Cannot redeclare identifier '",
                    declared->content_span, "' in catch clause.");
            }
            break;
        case BINDING_ARRAY:
            for_each (i, vec_binding, &declared->array) catch_check_block_names(catch_var, i, errs);
            break;
        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &declared->object) catch_check_block_names(catch_var, &i->target, errs);
            break;
    }
}

// A `let`/`const` directly in a catch block can't redeclare the catch variable (tsc's checkTryStatement)
static void catch_check_redeclared(Binding const* catch_var, Block const* block, ErrorCtx* errs) {
    for_each (i, vec_statement, &block->statements) {
        if (i->tag != STATEMENT_DECL_VARIABLE || i->decl.variable.keyword == VAR_INIT_VAR) continue;
        for_each (j, vec_initialized_binding, &i->decl.variable.initializers) catch_check_block_names(catch_var, &j->binding, errs);
    }
}

// What the builder couldn't know: code after a call that never returns or an exhaustive switch (tsc's
// isSourceElementUnreachable by the flow graph)
static bool statement_unreachable_by_flow(Statement const* me) {
    return my flow && statement_is_executable(me) && ty_flow_reachability(current_checker, my flow) == FLOW_REACH_NO;
}

// The statements of a block, a file, a namespace or a case clause. tsc's checkSourceElementUnreachable: a statement the
// flow graph can't reach starts a run of TS7027 over the unreachable statements that follow it (an error only when
// allowUnreachableCode is explicitly false), and nothing within the run (or the binder's, Statement.unreachable) is
// reported again
void statements_typecheck(VecStatement* list, BlockTypecheckCtx ctx, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    if (!options->is_set[COMPILER_OPTION_allow_unreachable_code] || options->allow_unreachable_code) {
        for_each (i, vec_statement, list) statement_typecheck(i, ctx, errs);
        return;
    }
    Checker* checker = current_checker;
    isize_t count = vec_statement_size(list);
    for (isize_t i = 0; i < count; i++) {
        Statement* statement = vec_statement_at_mut(list, i);
        bool was_within = checker->within_unreachable;
        isize_t last = i;
        bool reported = !was_within && statement_unreachable_by_flow(statement);
        if (reported) {
            while (last + 1 < count) {
                Statement const* next = vec_statement_at(list, last + 1);
                if (!statement_is_executable(next) || !(next->unreachable || statement_unreachable_by_flow(next))) break;
                last++;
            }
            error_ctx_error_d(errs, span_between(statement->span, vec_statement_at(list, last)->span), 7027,
                "Unreachable code detected.");
        }
        for (isize_t j = i; j <= last; j++) {
            Statement* run = vec_statement_at_mut(list, j);
            checker->within_unreachable = was_within || reported || run->unreachable;
            statement_typecheck(run, ctx, errs);
            checker->within_unreachable = was_within;
        }
        i = last;
    }
}

void statement_typecheck(Statement* me, BlockTypecheckCtx ctx, ErrorCtx* errs) {
    CurrentNode saved = ty_enter_node(current_checker, my span); // tsc's checkSourceElement
    // (An error at a class, interface, type alias or enum declaration goes at its name: tsc's getErrorRangeForNode)
    if (my tag == STATEMENT_DECL_CLASS && span_valid(my decl.class->name)) current_checker->current_node.error_span = my decl.class->name;
    if (my tag == STATEMENT_DECL_INTERFACE) current_checker->current_node.error_span = my decl.interface->name;
    if (my tag == STATEMENT_DECL_TYPE) current_checker->current_node.error_span = my decl.type->name;
    if (my tag == STATEMENT_DECL_ENUM) current_checker->current_node.error_span = my decl.enum_->name;
    statement_typecheck_inner(me, ctx, errs);
    ty_leave_node(current_checker, saved);
}

static void statement_typecheck_inner(Statement* me, BlockTypecheckCtx ctx, ErrorCtx* errs) {
    bool body = ctx.body;
    ctx.body = false;
    bool case_clause = ctx.case_clause;
    ctx.case_clause = false;
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
                expr_typecheck_if_condition(vec_expr_at(&my if_else.conditions, i), errs);
                Statement const* then = vec_statement_at(&my if_else.statements, i);
                if (then->tag == STATEMENT_NOOP && span_valid(then->span) && *span_start_ptr(then->span) == ';') { // (tsc's checkIfStatement)
                    char const* at = span_start_ptr(then->span);
                    error_ctx_error_d(errs, span_make_in(then->span, at, at + 1), 1313, "The body of an 'if' statement cannot be the empty statement.");
                }
            }
            ctx.body = true;
            for_each (i, vec_statement, &my if_else.statements) {
                statement_typecheck(i, ctx, errs);
            }
            for (intptr_t i = 0; i < cond_size; i++) {
                expr_check_if_known_truthy(vec_expr_at(&my if_else.conditions, i), vec_statement_at(&my if_else.statements, i), errs);
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

            // (`for await` in an async function below ES2018 needs __asyncValues, tsc's ForAwaitOfIncludes)
            if (my for_each_loop->is_await && ctx.function && ctx.function->is_async &&
                compiler_options()->target < SCRIPT_TARGET_ES2018
            ) {
                ty_check_emit_helpers(checker, my span, EMIT_HELPER_ASYNC_VALUES, errs);
            }
            if (my for_each_loop->variable.tag == FOR_EACH_LOOP_STATEMENT) {
                variable_statement_check_using(&my for_each_loop->variable.statement,
                    my for_each_loop->keyword == FOR_EACH_LOOP_IN, false, ctx.ambient, errs);
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
                                    expr_outer_span(&my for_each_loop->variable.expr),
                                    2491,
                                    "The left-hand side of a 'for...in' statement cannot be a destructuring pattern."
                                );
                            }
                            break;
                    }

                    // tsc's checkForInStatement: null and undefined are removed first (getNonNullableTypeIfNeeded)
                    Ty const* right_type = ty_non_nullable_if_needed(current_checker, iterable_type);
                    if ((right_type->flags & TYF_NEVER) || !for_in_accepts(current_checker, right_type)) {
                        error_ctx_error_d(errs, expr_outer_span(&my for_each_loop->iterable), 2407,
                            "The right-hand side of a 'for...in' statement must be of type 'any', an object type "
                            "or a type parameter, but here has type '", right_type, "'.");
                    }
                    break;

                case FOR_EACH_LOOP_OF:
                    if (!for_of_checks_iterable(my for_each_loop)) break;
                    ty_iterated_type(checker, my for_each_loop->is_await ? ITERATION_FOR_AWAIT_OF : ITERATION_FOR_OF,
                        for_of_iterable(checker, &my for_each_loop->iterable, iterable_type, errs),
                        expr_outer_span(&my for_each_loop->iterable), errs);
                    break;
            }

            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    for_each (i, vec_initialized_binding, &my for_each_loop->variable.statement.initializers) {
                        variable_statement_check_let_names(&my for_each_loop->variable.statement, i, errs);
                        binding_typecheck(&i->binding, nullptr, errs);
                        if (my for_each_loop->keyword == FOR_EACH_LOOP_OF && compiler_options()->no_implicit_any &&
                            !compiler_options()->strict_null_checks
                        ) {
                            Ty const* element = ty_iterated_type(checker, my for_each_loop->is_await ? ITERATION_FOR_AWAIT_OF : ITERATION_FOR_OF,
                                for_of_iterable(checker, &my for_each_loop->iterable, iterable_type, nullptr),
                                expr_outer_span(&my for_each_loop->iterable), nullptr);
                            binding_report_iterated_widening(&i->binding, element, errs);
                        }
                    }
                    break;

                case FOR_EACH_LOOP_EXPR:
                    if (!for_in_private_variable(my for_each_loop)) expr_typecheck(&my for_each_loop->variable.expr, errs);
                    if (!my for_each_loop->variable.expr.assignment_pattern) {
                        bool in = my for_each_loop->keyword == FOR_EACH_LOOP_IN;
                        // tsc's checkReferenceExpression (for-in: only if the keys fit, see TS2405 below)
                        if (!in) {
                            expr_check_reference_is_reference(&my for_each_loop->variable.expr, 2487,
                                "The left-hand side of a 'for...of' statement must be a variable or a property access.", errs);
                        }
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
                        // tsc's checkForInStatement: one message whatever the variable's type. The keys are those
                        // of the object's type, whatever the variable's declared one (getIndexTypeOrString)
                        Ty const* keys = ty_for_in_index_type_or_string(checker, ty_expr_type(checker, &my for_each_loop->iterable));
                        if (!ty_is_related(checker, keys,
                                expr_get_type(&my for_each_loop->variable.expr), TY_RELATION_ASSIGNABLE)
                        ) {
                            error_ctx_error_d(errs, expr_outer_span(&my for_each_loop->variable.expr), 2405,
                                "The left-hand side of a 'for...in' statement must be of type 'string' or 'any'.");
                        } else {
                            expr_check_reference_is_reference(&my for_each_loop->variable.expr, 2406,
                                "The left-hand side of a 'for...in' statement must be a variable or a property access.", errs);
                        }
                        break;
                    }
                    ty_check_assignable_expr(
                        checker,
                        // HACK: tsc checks a pattern target property by property (checkDestructuringAssignment),
                        // so the elements get no excess property checks
                        ty_widened_objects(checker, lazy_type_get(&my for_each_loop->var_type)),
                        expr_get_type(&my for_each_loop->variable.expr),
                        expr_outer_span(&my for_each_loop->variable.expr),
                        &my for_each_loop->iterable,
                        errs
                    );
                    break;
            }
            // (After the variables, like tsc: whether it's assigned walks the loop, whose variables it then types)
            expr_typecheck(&my for_each_loop->iterable, for_of_checks_iterable(my for_each_loop) ? errs : nullptr);

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
                        ty_check_comparable(current_checker, pattern_type, needle_type, expr_outer_span(&i->pattern), errs);
                    }
                } else {
                    default_count++;
                    // (tsc's checkSwitchStatement: the second one, a grammar error)
                    if (default_count == 2) {
                        error_ctx_grammar_error_d(
                            errs,
                            i->default_span,
                            1113,
                            "A 'default' clause cannot appear more than once in a 'switch' statement."
                        );
                    }
                }
                BlockTypecheckCtx clause_ctx = ctx;
                clause_ctx.case_clause = true;
                statements_typecheck(&i->statements, clause_ctx, errs);
                if (i->fallthrough && ty_flow_reachability(current_checker, i->fallthrough) == FLOW_REACH_YES) {
                    error_ctx_error_d(errs, i->head, 7029, "Fallthrough case in switch.");
                }
            }
        }   break;

        case STATEMENT_TRY_CATCH:
            block_typecheck(&my try_catch->try_block, ctx, errs);
            binding_typecheck(&my try_catch->catch_var, nullptr, errs);
            if (my try_catch->catch_var.has_explicit_type &&
                !(ty_of_node(binding_type(&my try_catch->catch_var))->flags & (TYF_ANY | TYF_UNKNOWN))
            ) {
                error_ctx_error_d(errs, binding_explicit_type_span(&my try_catch->catch_var), 1196,
                    "Catch clause variable type annotation must be 'any' or 'unknown' if specified.");
            }
            if (my try_catch->has_catch && !my try_catch->catch_var.has_explicit_type && !my try_catch->catch_var.default_value) {
                catch_check_redeclared(&my try_catch->catch_var, &my try_catch->catch_block, errs);
            }
            block_typecheck(&my try_catch->catch_block, ctx, errs);
            block_typecheck(&my try_catch->finally_block, ctx, errs);
            break;

        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
            statement_check_jump(me, ctx, errs);
            break;

        case STATEMENT_THROW_EXPR:
            if (my expr.tag == EXPR_IMPLICIT_ANY && span_valid(my expr.inner_span) && !my expr.inner_span.size &&
                span_valid(my span)
            ) {
                // tsc's checkThrowStatement: `throw` then a line break (the parser's missing expression), right after
                // the keyword
                char const* at = span_start_ptr(my span) + strlen("throw");
                error_ctx_grammar_error_d(errs, span_make_in(my span, at, at), 1142, "Line break not permitted here.");
            }
            expr_typecheck(&my expr, errs);
            break;

        case STATEMENT_EXPR:
            expr_typecheck(&my expr, errs);
            ty_check_assertion_call_statement(current_checker, &my expr, errs);
            break;

        case STATEMENT_EXPORT_DEFAULT_EXPR:
            if (my export_misplaced) { // (tsc's checkExportAssignment bails out once the expression is checked)
                expr_typecheck_export_assignment(&my expr, errs);
                break;
            }
            if (my expr_jsdoc_type) {
                // (Checked with it as the contextual type, then related without elaborating: tsc's checkTypeAssignableTo)
                type_typecheck(my expr_jsdoc_type, errs);
                Ty const* declared = ty_of_node(my expr_jsdoc_type);
                Ty const* type = ty_check_expr(current_checker, &my expr, declared);
                expr_typecheck(&my expr, errs);
                ty_check_assignable(current_checker, type, declared, expr_outer_span(&my expr), errs);
                break;
            }
            if (my export_equals && compiler_options()->erasable_syntax_only && !ctx.ambient) { // (tsc's checkExportAssignment)
                error_ctx_error_d(errs, span_with_semicolon(my span), 1294, "This syntax is not allowed when 'erasableSyntaxOnly' is enabled.");
            }
            expr_typecheck_export_assignment(&my expr, errs);
            // (`undefined` is an identifier to tsc's isEntityNameExpression)
            if (ctx.ambient && !expr_is_entity_name(&my expr) &&
                !(my expr.tag == EXPR_LITERAL && my expr.literal.tag == VALUE_UNDEFINED && span_equals(my expr.inner_span, "undefined"))
            ) {
                error_ctx_grammar_error_d(errs, expr_outer_span(&my expr), 2714, "The expression of an export assignment must be "
                    "an identifier or qualified name in an ambient context.");
            }
            break;

        case STATEMENT_RETURN_EXPR:
            if (ctx.function && !ctx.function->ret_explicit && !ctx.constructed_class && !ctx.in_setter) {
                ty_function_return_type(current_checker, ctx.function);
            }
            ty_check_expr(current_checker, &my expr, current_checker->return_context);
            expr_typecheck(&my expr, errs);
            // tsc's checkAndAggregateReturnExpressionTypes: an async function's inferred return awaits what it returns
            if (ctx.function && ctx.function->is_async && !ctx.function->is_generator && !ctx.function->ret_explicit &&
                span_valid(ctx.function->name) && !statement_is_bare_return(me) &&
                ty_awaits_thenable(current_checker, ty_expr_type(current_checker, &my expr))
            ) {
                error_ctx_error_d(errs, ctx.function->name, 1058, "The return type of an async function must either be a "
                    "valid promise or must not contain a callable 'then' member.");
            }
            // tsc's checkReturnExpression: with an annotation, what an async function returns is awaited, at the statement
            if (ctx.function && ctx.function->is_async && ctx.function->ret_explicit && !statement_is_bare_return(me) &&
                !ctx.constructed_class && !ctx.in_setter && returns_thenable(&my expr)
            ) {
                // (An arrow's expression body: at the expression)
                Span at = span_valid(my span) ? span_make_in(my span, span_start_ptr(my span), span_start_ptr(my span) + 6) :
                    expr_outer_span(&my expr);
                error_ctx_error_d(errs, at, 1058,
                    "The return type of an async function must either be a valid promise or must not contain a callable 'then' member.");
            }
            if (ctx.in_setter) {
                if (!statement_is_bare_return(me)) {
                    error_ctx_error_d(errs, span_make_in(my span, span_start_ptr(my span), span_start_ptr(my span) + 6), 2408,
                        "Setters cannot return a value.");
                }
            } else if (ctx.constructed_class && !statement_is_bare_return(me)) {
                // What a constructor returns replaces the instance (tsc's checkReturnStatement)
                if (!return_typecheck(me, &my expr, ty_instance_type_of_class(current_checker, ctx.constructed_class), false, false, errs)) {
                    error_ctx_error_d(errs, span_make_in(my span, span_start_ptr(my span), span_start_ptr(my span) + 6), 2409,
                        "Return type of constructor signature must be assignable to the instance type of the class.");
                }
            } else if (ctx.unwrapped_return) {
                // (A bare `return;` returns `undefined` to what an async function awaits to or a generator's TReturn
                // too, with strictNullChecks: tsc's checkReturnStatement only checks it without for a declared `never`,
                // which `Promise<never>` isn't)
                if (!statement_is_bare_return(me) || compiler_options()->strict_null_checks) {
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
                    error_ctx_error_d(errs, span_make_in(my span, span_start_ptr(my span), span_start_ptr(my span) + 6), 7030,
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
            // (Not after an illegal modifier, `if (a) export const x = 1;`: tsc's checkVariableStatement stops at TS1184)
            if (body && my decl.variable.keyword != VAR_INIT_VAR && !my decl.props.exported && !span_valid(my decl.props.declare_keyword)) {
                // tsc's checkGrammarForDisallowedBlockScopedVariableStatement() (an error, said with parse errors too)
                error_ctx_error_d(errs, span_with_semicolon(my span), 1156,
                    "'", variable_statement_keyword_str(&my decl.variable), "' declarations can only be declared inside a block.");
            }
            // (A `declare` or `export` modifier on a `using` is an error of its own, which stops tsc's grammar checks)
            variable_statement_typecheck(&my decl.variable, my decl.props.declared || ctx.ambient, my decl.props.exported,
                case_clause, (my decl.props.declared || ctx.ambient) && !span_valid(my decl.props.declare_keyword) && !my decl.props.exported, errs);
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
            if (body) { // (tsc's checkTypeAliasDeclaration: allowBlockDeclarations)
                error_ctx_grammar_error_d(errs, my decl.type->name, 1156, "'type' declarations can only be declared inside a block.");
            }
            type_decl_typecheck(my decl.type, errs);
            break;

        case STATEMENT_DECL_INTERFACE:
            if (body) { // (tsc's checkInterfaceDeclaration: allowBlockDeclarations)
                error_ctx_grammar_error_d(errs, my decl.interface->name, 1156, "'interface' declarations can only be declared inside a block.");
            }
            interface_decl_typecheck(my decl.interface, errs);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_typecheck(my decl.namespace, my decl.props.declared || ctx.ambient, errs);
            break;
    }
}

void switch_case_drop(SwitchCase* me) {
    if (my is_pattern) {
        expr_drop(&my pattern);
    }
    vec_statement_drop(&my statements);
}

ForEachLoopStatement const* scope_for_in_loop_of(Scope const* names, Symbol const* variable) {
    for (Scope const* scope = names; scope; scope = scope->parent) {
        ForEachLoopStatement const* loop = scope_rare(scope)->for_in_body;
        if (!loop) continue;
        Symbol const* symbol = nullptr;
        if (loop->variable.tag == FOR_EACH_LOOP_STATEMENT) {
            VariableStatement const* statement = &loop->variable.statement;
            if (vec_initialized_binding_size(&statement->initializers) && statement->initializers.data[0].binding.tag == BINDING_IDENTIFIER) {
                symbol = statement->initializers.data[0].binding.symbol;
            }
        } else if (loop->variable.expr.tag == EXPR_IDENTIFIER) {
            symbol = loop->variable.expr.identifier.target;
        }
        if (symbol && symbol == variable && (symbol->flags & SYMBOL_VARIABLE)) return loop;
    }
    return nullptr;
}

// tsc's checkExportAssignment under isolatedModules/verbatimModuleSyntax: `export default x`/`export = x` of a type,
// or of something imported or exported as a type only, which a transpiler can't tell from a value; `export default`
// in a file emitted as CommonJS
static void export_assignment_check_isolated(Statement const* me, Scope* names, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    if (!options->isolated_modules && !options->verbatim_module_syntax) return;
    SourceFile const* file = names->file;
    if (!file) return;
    bool ambient = file->is_declaration;
    for (Scope const* scope = names; scope && !ambient; scope = scope->parent) ambient = scope->ambient;
    if (ambient) return;
    bool export_equals = my export_equals;
    bool illegal_in_cjs = !export_equals && options->verbatim_module_syntax &&
        source_file_emit_module_kind(file) == MODULE_KIND_COMMONJS;
    if (illegal_in_cjs) {
        source_file_verbatim_cjs_error(file, errs, span_with_semicolon(my span), false);
        return;
    }
    if (my expr.tag != EXPR_IDENTIFIER || my expr.identifier.keyword || expr_is_perenthisized(&my expr)) return;
    Span id = my expr.inner_span;
    Symbol* symbol = scope_resolve(names, span_sv(id), SYMBOL_ANY_MEANING);
    if (!symbol || symbol->is_placeholder) return;
    // (tsc's getTypeOnlyAliasDeclaration(symbol, Value): only when what it names is a value)
    Symbol const* named_by = nullptr;
    PortItem const* type_only = symbol_type_only_declaration_naming(symbol, &named_by);
    if (type_only) {
        Symbol* named = named_by ? (Symbol*)named_by : symbol;
        if (named->flags & SYMBOL_ALIAS) named = symbol_resolve_alias(named);
        if (!named || !(symbol_resolved_flags(named) & SYMBOL_VALUE)) type_only = nullptr;
    }
    const char* what = export_equals ? "An 'export =' declaration" : "An 'export default'";
    if (options->verbatim_module_syntax) {
        if (!(symbol_resolved_flags(symbol) & SYMBOL_VALUE)) {
            error_ctx_error_d(errs, id, export_equals ? 1282 : 1284, what, " must reference a value when "
                "'verbatimModuleSyntax' is enabled, but '", id, "' only refers to a type.");
        } else if (type_only) {
            error_ctx_error_d(errs, id, export_equals ? 1283 : 1285, what, " must reference a real value when "
                "'verbatimModuleSyntax' is enabled, but '", id, "' resolves to a type-only declaration.");
        }
    }
    if (symbol->flags & SYMBOL_VALUE) return;
    const char* flag = options->verbatim_module_syntax ? "verbatimModuleSyntax" : "isolatedModules";
    Symbol const* target = symbol->flags & SYMBOL_ALIAS ? symbol_resolve_alias(symbol) : nullptr;
    u32 non_local = target && !target->is_placeholder ? symbol_resolved_flags(target) : 0;
    SourceFile const* type_only_file = type_only ? span_source(type_only->decl_name) : nullptr;
    bool elsewhere = type_only && type_only_file != file;
    if (non_local & SYMBOL_TYPE && !(non_local & SYMBOL_VALUE) && (!type_only || elsewhere)) {
        if (export_equals) {
            error_ctx_error_d(errs, id, 1291, "'", id, "' resolves to a type and must be marked type-only in this file "
                "before re-exporting when '", flag, "' is enabled. Consider using 'import type' where '", id, "' is imported.");
        } else {
            error_ctx_error_d(errs, id, 1292, "'", id, "' resolves to a type and must be marked type-only in this file "
                "before re-exporting when '", flag, "' is enabled. Consider using 'export type { ", id, " as default }'.");
        }
    } else if (elsewhere) {
        Error* error = export_equals ?
            error_ctx_error_d(errs, id, 1289, "'", id, "' resolves to a type-only declaration and must be marked "
                "type-only in this file before re-exporting when '", flag, "' is enabled. Consider using 'import type' "
                "where '", id, "' is imported.") :
            error_ctx_error_d(errs, id, 1290, "'", id, "' resolves to a type-only declaration and must be marked "
                "type-only in this file before re-exporting when '", flag, "' is enabled. Consider using "
                "'export type { ", id, " as default }'.");
        port_item_add_type_only_related(error, type_only, span_sv(id));
    }
}

// The `export` keyword of a declaration statement (after its decorators, if any)
static Span export_keyword_of(Span statement) {
    char const* start = span_start_ptr(statement);
    char const* end = span_end_ptr(statement);
    for (char const* p = start; p + 6 <= end; p++) {
        if (memcmp(p, "export", 6) != 0) continue;
        bool before = p > start && (isalnum((unsigned char)p[-1]) || p[-1] == '_' || p[-1] == '$');
        bool after = p + 6 < end && (isalnum((unsigned char)p[6]) || p[6] == '_' || p[6] == '$');
        if (!before && !after) return span_make_in(statement, p, p + 6);
    }
    return span_null;
}

void source_file_check_verbatim_exports(SourceFile const* file, ErrorCtx* errs) {
    // tsc's checkGrammarModifiers and checkModuleDeclaration under verbatimModuleSyntax: a top-level `export` of
    // something with a value can't be written as is in a file emitted as CommonJS (a namespace only when instantiated)
    if (!compiler_options()->verbatim_module_syntax || file->is_declaration) return;
    if (source_file_emit_module_kind(file) != MODULE_KIND_COMMONJS) return;
    const char* message = "A top-level 'export' modifier cannot be used on value declarations in a CommonJS module when "
        "'verbatimModuleSyntax' is enabled.";
    for_each (i, vec_statement, &file->ast.statements) {
        bool grammar = true;
        switch (i->tag) {
            case STATEMENT_DECL_VARIABLE:
            case STATEMENT_DECL_FUNCTION:
            case STATEMENT_DECL_CLASS:
            case STATEMENT_DECL_ENUM:
                if (i->decl.props.exported == DECL_EXPORTED_NO || i->decl.props.declared) continue;
                break;
            case STATEMENT_DECL_NAMESPACE:
                if (i->decl.props.exported == DECL_EXPORTED_NO || i->decl.props.declared) continue;
                if (i->decl.namespace->is_global || i->decl.namespace->is_ambient_module) continue;
                if (!namespace_decl_is_instantiated(i->decl.namespace)) continue;
                grammar = false;
                break;
            case STATEMENT_IMPORT_EXPORT:
                // (`export import a = N.b`)
                if (!i->import_export->main.export_import || i->import_export->main.declared) continue;
                break;
            default:
                continue;
        }
        Span at = export_keyword_of(i->span);
        if (!span_valid(at)) continue;
        if (grammar) {
            error_ctx_grammar_error_d(errs, at, 1287, message);
        } else {
            error_ctx_error_d(errs, at, 1287, message);
        }
    }
}
