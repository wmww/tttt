#pragma once

#include "ast_alloc.h"
#include "block.h"
#include "expr.h"
#include "type.h"
#include "function_impl.h"
#include "interface_decl.h"
#include "binding.h"
#include "scope.h"

typedef struct ErrorCtx ErrorCtx;
typedef struct EnumDecl EnumDecl;
typedef struct SourceDirectiveStatement SourceDirectiveStatement;
typedef struct ForLoopStatement ForLoopStatement;
typedef struct TryCatchStatement TryCatchStatement;
typedef struct ForEachLoopStatement ForEachLoopStatement;
typedef struct SwitchStatement SwitchStatement;
typedef struct PortStatement PortStatement;
typedef struct WithStatement WithStatement;

// Variable initialization

typedef struct VariableStatement {
    enum : uint8_t {
        VAR_INIT_VAR,
        VAR_INIT_LET,
        VAR_INIT_CONST,
        VAR_INIT_USING,
        VAR_INIT_AWAIT_USING,
    } keyword;
    bool at_top_level; // `await using` outside of any function
    bool in_sync_function; // `await using` in a function that isn't async (TS2852, said by the resolve walk)
    bool in_static_block; // `await using` in a class `static {}` block
    VecInitializedBinding initializers;
    Span span; // From the keyword to the end of the last declaration (`await` of `await using`: variable_statement_await_span())
} VariableStatement;

// `await using`: the `await`
static inline Span variable_statement_await_span(VariableStatement const* me) {
    return span_make_in(my span, span_start_ptr(my span), span_start_ptr(my span) + 5);
}

void variable_statement_drop(VariableStatement* me);
// const, using and await using (tsc's isVarConstLike)
static inline bool variable_statement_is_const(VariableStatement const* me) {
    return my keyword == VAR_INIT_CONST || my keyword == VAR_INIT_USING || my keyword == VAR_INIT_AWAIT_USING;
}
static inline char const* variable_statement_keyword_str(VariableStatement const* me) {
    switch (my keyword) {
        case VAR_INIT_VAR: return "var";
        case VAR_INIT_LET: return "let";
        case VAR_INIT_CONST: return "const";
        case VAR_INIT_USING: return "using";
        case VAR_INIT_AWAIT_USING: return "await using";
    }
    return "";
}
void variable_statement_add_names(VariableStatement* me, Scope* scope, DeclStatementProps props);
void variable_statement_resolve_names(VariableStatement* me, Scope* names, ErrorCtx* errs);
void variable_statement_typecheck(VariableStatement* me, bool declared, bool exported, bool case_clause, bool ambient_using, ErrorCtx* errs);
// `/** @enum {T} */`: the members of the object literal `object` that initializes `symbol` checked against T

// Type declaration

typedef struct TypeDecl {
    Span name;
    Span full_span;
    Generic generic;
    TypeNode type;
    Symbol* symbol;
    OuterGenerics const* outer_generics; // The type parameters it captures (set by the resolve walk)
    // Declared in a block or function body (set by the resolve walk): the types baseline can't name it (tsc's
    // isTypeSymbolAccessible, a declaration there isn't visible) and prints its structure instead
    bool local;
    // Declared inside a function (set by the resolve walk; tsc's isLocalTypeAlias, getContainingFunction): its body's
    // instantiation of an outer alias keeps that alias's name (a block's or a static block's outside one does not count)
    bool in_function;
    // A JavaScript file's `@typedef`/`@callback` (jsdoc_typedef_statements()): not part of the baselines' walk, and
    // exported from a module's top level (tsc's jsdocTreatAsExported)
    bool jsdoc;
    // A JSDoc one's end in tsgo (its tag's, children included: the next tag's `@`...), what declaration maps map
    char const* jsdoc_end;
    // Not a declaration: a JSDoc type that only gets resolved and checked (tsc checks every tag of a comment), like the
    // constraint of a `@template {C}` without a type parameter. No name, no symbol
    bool jsdoc_check_only;
} TypeDecl;

void type_decl_drop(TypeDecl* me);
void type_decl_add_names(TypeDecl* me, Scope* scope, DeclStatementProps props);
void type_decl_typecheck(TypeDecl* me, ErrorCtx* errs);

// Scope declaration

typedef struct NamespaceDecl {
    Span name;
    Span full_span;
    bool is_global;
    bool is_invalid_global; // `declare global` where it augments nothing: a namespace named global
    bool is_ambient_module; // `declare module "name" {}`, name is the string literal including quotes
    bool is_shorthand; // `declare module "name";`: everything imported from it is any
    ModuleRef* module_ref; // Of an ambient module
    // `declare module "*.css" with { type: "css" } {}`: the type literal after `with` (a TYPE_INTERFACE), owned. Which
    // imports a pattern module is for (tsgo's ModuleDeclaration.Attributes, see module_attributes.h)
    TypeNode* attributes;
    bool is_augmentation; // `declare module "./a" {}` in a module: adds to the module it names
    bool labeled; // The body of a labeled statement (a context it isn't allowed in, TS1234/TS1235)
    bool module_keyword; // `module N {}` (and each part of `module A.B {}`): TS1540, `namespace` is the way now
    bool misplaced; // Where no namespace can be (TS1234/TS1235), nothing else is said about it
    bool jsdoc; // Of a dotted `@typedef` or `@callback` name (`A.B` of `A.B.C`): allowed anywhere, exported from a module
    Block contents;
    Scope* inner_scope;
    Symbol* symbol;
} NamespaceDecl;

void namespace_decl_drop(NamespaceDecl* me);
void namespace_decl_add_names(NamespaceDecl* me, Scope* scope, DeclStatementProps props);
// The declarations of its block (namespace_decl_add_names() defers a module's `declare global` to the binder)
void namespace_decl_add_contents(NamespaceDecl* me);
void namespace_decl_resolve_names(NamespaceDecl* me, Scope* names, ErrorCtx* errs);
void namespace_decl_typecheck(NamespaceDecl* me, bool declared, ErrorCtx* errs);
// tsc's getModuleInstanceState: the namespace has a value (it declares something other than types)
// A string, number or boolean literal or what may be a literal enum reference (tsc's checkAmbientInitializer)
bool expr_is_ambient_const_initializer(Expr const* me);
bool namespace_decl_is_instantiated(NamespaceDecl const* me);
bool namespace_decl_declares_value(NamespaceDecl const* me);

// If statements

typedef struct IfStatement {
    VecExpr conditions;
    // The length of statements should always be equal to conditions, or one greater (in the case of a trailing else)
    VecStatement statements;
} IfStatement;

// Statements

typedef struct Statement {
    enum {
        STATEMENT_NOOP,
        STATEMENT_SOURCE_DIRECTIVE,
        STATEMENT_BLOCK,
        STATEMENT_IF_ELSE,
        STATEMENT_FOR_LOOP,
        STATEMENT_FOR_EACH_LOOP,
        STATEMENT_SWITCH,
        STATEMENT_TRY_CATCH,
        STATEMENT_LABELED,
        STATEMENT_WITH, // `with (x) body`: tsc binds the body but doesn't check it
        STATEMENT_BREAK,
        STATEMENT_CONTINUE,
        STATEMENT_EXPR,
        STATEMENT_EXPORT_DEFAULT_EXPR, // also uses expr union member
        STATEMENT_RETURN_EXPR, // also uses expr union member
        STATEMENT_THROW_EXPR, // also uses expr union member
        STATEMENT_IMPORT_EXPORT,
        STATEMENT_DECL_VARIABLE,
        STATEMENT_DECL_FUNCTION,
        STATEMENT_DECL_ENUM,
        STATEMENT_DECL_CLASS,
        STATEMENT_DECL_TYPE,
        STATEMENT_DECL_INTERFACE,
        STATEMENT_DECL_NAMESPACE,
    } tag;
    bool export_equals; // STATEMENT_EXPORT_DEFAULT_EXPR: `export = x` rather than `export default x`
    // STATEMENT_EXPORT_DEFAULT_EXPR not at the top level of a file or namespace (TS1231/TS1258): its expression is
    // neither resolved nor checked (tsc's checkExportAssignment bails out)
    bool export_misplaced;
    // In a range of unreachable code the binder found (TS7027 if reported at all): nothing in it is reported again
    // (tsc's withinUnreachableCode)
    bool unreachable;
    union {
        SourceDirectiveStatement* source_directive;
        Block block;
        struct {
            Expr expr; // also used for return and export default expressions
            // Owned, JavaScript `/** @type {T} */ export default v`: the type it is checked against
            TypeNode* expr_jsdoc_type;
        };
        IfStatement if_else;
        ForLoopStatement* for_loop;
        ForEachLoopStatement* for_each_loop;
        SwitchStatement* switch_;
        TryCatchStatement* try_catch;
        WithStatement* with;
        struct {
            Span keyword_span; // Used for continue and break
            Span jump_label; // The label break and continue jump to, null if there isn't one
        };
        struct {
            Span name;
            Statement* body; // Owned, never null
        } labeled;
        PortStatement* import_export;
        struct {
            DeclStatementProps props;
            union {
                VariableStatement variable;
                struct {
                    FunctionImpl* impl;
                } function;
                EnumDecl* enum_;
                ClassDecl* class;
                TypeDecl* type; // Owned, never null
                InterfaceDecl* interface;
                NamespaceDecl* namespace; // Owned, never null
            };
        } decl;
    };
    Span span; // With the `;`
    // The control flow node before the statement, for the statements tsc gives one (null otherwise, and in code the
    // builder found unreachable): a call that never returns or an exhaustive switch before it makes it unreachable
    FlowNode* flow;
} Statement;

#define statement_noop ((Statement){ .tag = STATEMENT_NOOP })

static inline bool statement_tag_is_decl(typeof((Statement){}.tag) tag) {
    switch (tag) {
        case STATEMENT_DECL_VARIABLE:
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_ENUM:
        case STATEMENT_DECL_CLASS:
        case STATEMENT_DECL_TYPE: return true;
        default: return false;
    }
}

// The statements that have decl.props
static inline bool statement_declares_name(typeof((Statement){}.tag) tag) {
    return statement_tag_is_decl(tag) || tag == STATEMENT_DECL_INTERFACE || tag == STATEMENT_DECL_NAMESPACE;
}

static inline DeclExported statement_get_exported(Statement const* me) {
    if (statement_tag_is_decl(my tag)) {
        return my decl.props.exported;
    } else if (my tag == STATEMENT_EXPORT_DEFAULT_EXPR) {
        return DECL_EXPORTED_YES_DEFAULT;
    } else {
        return DECL_EXPORTED_NO;
    }
}

void statement_drop(Statement* me);
void statement_add_names(Statement* me, Scope* scope);
void statement_hoist_names(Statement* me, Scope* scope);
// The names of the `var`s nested in a statement, hoisted out of it in strict code (see statement_hoist_names())
void statement_collect_var_names(Statement const* me, VecSpan* out);
// The names of the values a function declares in its own scope (tsc's locals of the function with a value meaning): its
// parameters, the declarations directly in its body and every `var` of its body. The function is strict code
void function_collect_local_value_names(struct FunctionImpl const* function, VecSpan* out);
// The resolve walk, which also builds the control flow graph (flow.h): statements are walked in flow order
void statement_resolve_names(Statement* me, Scope* names, ErrorCtx* errs);
// The types of `return` expressions. `return;` has none (FunctionImpl.has_bare_return)
void statement_collect_return_types(Statement const* me, TyBuf* out);
static inline bool statement_is_bare_return(Statement const* me) {
    // The parser gives `return;` an `undefined` literal that spans the keyword
    return my tag == STATEMENT_RETURN_EXPR && my expr.tag == EXPR_LITERAL && my expr.literal.tag == VALUE_UNDEFINED &&
        csview_starts_with(span_sv(expr_outer_span(&my expr)), "return");
}
// Whether pred holds for the expression of some non-bare `return` (not looking into nested functions, tsc's
// forEachReturnStatement)
bool statement_any_return(Statement const* me, bool (*pred)(Expr const*));
// tsc's checkFunctionOrConstructorSymbol for the function declarations of a statement list: every overload is followed
// by the next one or the implementation (TS2391, TS2389). Syntactic, per list: tsc's symbol walk only compares
// declarations with the same parent
void statements_check_overloads(VecStatement const* statements, ErrorCtx* errs);
void statement_typecheck(Statement* me, BlockTypecheckCtx ctx, ErrorCtx* errs);
void statements_typecheck(VecStatement* list, BlockTypecheckCtx ctx, ErrorCtx* errs);

#define g_class_key Statement, statement
#define g_declared
#define g_no_clone
#define g_allocator ast
#include <cleat/generic/vec.h>

bool statements_any_return(VecStatement const* statements, bool (*pred)(Expr const*));

// Source directive statement (the XML thingys defined at the top level)

typedef struct SourceDirectiveStatement {
    enum {
        SOURCE_DIRECTIVE_REFERENCE_PATH,
        SOURCE_DIRECTIVE_REFERENCE_LIB,
        SOURCE_DIRECTIVE_REFERENCE_TYPES,
    } tag;
    cstr path;
    Span path_span;
    bool preserve;
    // `resolution-mode="import"`/`"require"` on a types reference (ImportResolutionOverride, import_attributes.h)
    uint8_t resolution_override;
    // Set by the program when what is named wasn't found: the path (TS6053) or name (TS2688) as written
    cstr not_found;
    bool not_found_extensionless; // A path without extension that no .ts/.tsx/.d.ts completes: TS6231
    bool not_found_network; // A network path (path_is_network()): TS5116, never looked up
    // A path to a referenced project's source whose output declaration file isn't built: TS6305 (absolute paths)
    cstr unbuilt_output, unbuilt_source;
    bool self_reference; // A path to the file it is in: TS1006
    struct SourceFile const* referenced; // A path's file, once the program loaded it (declaration emit rewrites it)
} SourceDirectiveStatement;

// Loop statements

typedef struct ForLoopStatement {
    Statement initialization; // initialization happens only once, but in the same stack frame as the body
    Expr condition;
    Expr afterthought;
    Statement body;
    bool is_do_while; // The body comes before the condition in the source
    bool is_while; // `while (c)`: a `continue` goes straight back to the condition (no incrementor label)
} ForLoopStatement;

typedef struct ForEachLoopStatement {
    enum {
        FOR_EACH_LOOP_IN,
        FOR_EACH_LOOP_OF,
    } keyword;
    bool is_await; // `for await (x of y)`
    uint8_t await_place; // An AwaitPlace (expr.h)
    Span await_span;
    struct {
        enum {
            FOR_EACH_LOOP_STATEMENT,
            FOR_EACH_LOOP_EXPR,
        } tag;
        union {
            VariableStatement statement;
            Expr expr;
        };
    } variable;
    LazyType var_type;
    Expr iterable;
    Statement body;
} ForEachLoopStatement;

zsview for_each_loop_statement_keyword_str(ForEachLoopStatement const* me);
Ty const* for_each_loop_statement_evaluate_var_type(ForEachLoopStatement* me);
// What a for-of's pattern destructures: the iterated type, not widened like the loop variable's (tsc's
// getTypeForBindingElementParent -> checkRightHandSideOfForOf). A for-in's is the variable's type
Ty const* for_each_loop_statement_pattern_source_type(ForEachLoopStatement const* me);

// Switch statement

typedef struct SwitchCase {
    bool is_pattern; // else default
    union {
        Expr pattern;
        Span default_span;
    };
    VecStatement statements;
    Span head; // `case x:` / `default:`
    // With noFallthroughCasesInSwitch: where control is at the end of the statements, if it may get there and this
    // isn't the last clause (tsc's fallthroughFlowNode, TS7029)
    struct FlowNode const* fallthrough;
} SwitchCase;

void switch_case_drop(SwitchCase* me);

#define g_class_key SwitchCase, switch_case
#define g_no_clone
#define g_allocator ast
#include <cleat/generic/vec.h>

typedef struct SwitchStatement {
    Expr needle;
    VecSwitchCase cases;
} SwitchStatement;

// With statement (tsc parses and binds it, the checker reports TS2410 and never looks into the body: every node in it
// is `any` with no symbol in the baselines)

typedef struct WithStatement {
    Expr object;
    Statement body;
    Span header; // `with (x)`: where TS2410 goes
} WithStatement;

// Try-catch-finally statement

typedef struct TryCatchStatement {
    Block try_block;
    Binding catch_var;
    Expr* catch_initializer; // Owned, `catch (e = 1)` (TS1197): only its names are looked up and it's typed
    Block catch_block;
    Block finally_block;
    bool has_catch;
    bool has_finally;
} TryCatchStatement;
