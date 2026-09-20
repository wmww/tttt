#pragma once

#include "type.h"
#include "expr.h"
#include "arena.h"

// Symbols, symbol tables and scopes: the binder (plan 04). The model is tsc's (SymbolFlags, the *Excludes merge
// masks, aliases, exports tables shared by every block of a namespace), the laziness isn't: names are resolved in a
// walk of their own before checking.

typedef struct PortItem PortItem;
typedef struct Program Program;
typedef struct Symbol Symbol;
typedef struct Scope Scope;
typedef struct Binder Binder;

// tsc's SymbolFlags, bit for bit where they exist here
typedef enum SymbolFlags {
    SYMBOL_NONE = 0,
    SYMBOL_FUNCTION_SCOPED_VARIABLE = 1 << 0,
    SYMBOL_BLOCK_SCOPED_VARIABLE = 1 << 1,
    SYMBOL_PROPERTY = 1 << 2,
    SYMBOL_ENUM_MEMBER = 1 << 3,
    SYMBOL_FUNCTION = 1 << 4,
    SYMBOL_CLASS = 1 << 5,
    SYMBOL_INTERFACE = 1 << 6,
    SYMBOL_CONST_ENUM = 1 << 7,
    SYMBOL_REGULAR_ENUM = 1 << 8,
    SYMBOL_VALUE_MODULE = 1 << 9,
    SYMBOL_NAMESPACE_MODULE = 1 << 10,
    SYMBOL_TYPE_PARAMETER = 1 << 18,
    SYMBOL_TYPE_ALIAS = 1 << 19,
    SYMBOL_ALIAS = 1 << 21,

    SYMBOL_ENUM = SYMBOL_REGULAR_ENUM | SYMBOL_CONST_ENUM,
    SYMBOL_VARIABLE = SYMBOL_FUNCTION_SCOPED_VARIABLE | SYMBOL_BLOCK_SCOPED_VARIABLE,
    SYMBOL_VALUE = SYMBOL_VARIABLE | SYMBOL_PROPERTY | SYMBOL_ENUM_MEMBER | SYMBOL_FUNCTION | SYMBOL_CLASS |
        SYMBOL_ENUM | SYMBOL_VALUE_MODULE,
    SYMBOL_TYPE = SYMBOL_CLASS | SYMBOL_INTERFACE | SYMBOL_ENUM | SYMBOL_ENUM_MEMBER | SYMBOL_TYPE_PARAMETER |
        SYMBOL_TYPE_ALIAS,
    SYMBOL_NAMESPACE = SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE | SYMBOL_ENUM,
    SYMBOL_ANY_MEANING = SYMBOL_VALUE | SYMBOL_TYPE | SYMBOL_NAMESPACE,

    SYMBOL_FUNCTION_SCOPED_VARIABLE_EXCLUDES = SYMBOL_VALUE & ~SYMBOL_FUNCTION_SCOPED_VARIABLE,
    SYMBOL_BLOCK_SCOPED_VARIABLE_EXCLUDES = SYMBOL_VALUE,
    SYMBOL_PARAMETER_EXCLUDES = SYMBOL_VALUE,
    SYMBOL_ENUM_MEMBER_EXCLUDES = SYMBOL_VALUE | SYMBOL_TYPE,
    SYMBOL_FUNCTION_EXCLUDES = SYMBOL_VALUE & ~(SYMBOL_FUNCTION | SYMBOL_VALUE_MODULE | SYMBOL_CLASS),
    SYMBOL_CLASS_EXCLUDES = (SYMBOL_VALUE | SYMBOL_TYPE) & ~(SYMBOL_VALUE_MODULE | SYMBOL_INTERFACE | SYMBOL_FUNCTION),
    SYMBOL_INTERFACE_EXCLUDES = SYMBOL_TYPE & ~(SYMBOL_INTERFACE | SYMBOL_CLASS),
    SYMBOL_REGULAR_ENUM_EXCLUDES = (SYMBOL_VALUE | SYMBOL_TYPE) & ~(SYMBOL_REGULAR_ENUM | SYMBOL_VALUE_MODULE),
    SYMBOL_CONST_ENUM_EXCLUDES = (SYMBOL_VALUE | SYMBOL_TYPE) & ~SYMBOL_CONST_ENUM,
    SYMBOL_VALUE_MODULE_EXCLUDES = SYMBOL_VALUE &
        ~(SYMBOL_FUNCTION | SYMBOL_CLASS | SYMBOL_REGULAR_ENUM | SYMBOL_VALUE_MODULE),
    SYMBOL_NAMESPACE_MODULE_EXCLUDES = 0,
    SYMBOL_TYPE_PARAMETER_EXCLUDES = SYMBOL_TYPE & ~SYMBOL_TYPE_PARAMETER,
    SYMBOL_TYPE_ALIAS_EXCLUDES = SYMBOL_TYPE,
    SYMBOL_ALIAS_EXCLUDES = SYMBOL_ALIAS,
} SymbolFlags;

typedef struct SymbolEntry {
    u64 hash;
    csview name; // Points into the source (or a literal), never owned
    Symbol* symbol;
} SymbolEntry;

// Insertion ordered. Small tables (most blocks declare 0-3 names) are scanned, bigger ones get an index
typedef struct SymbolTable {
    // Read-only table this one overlays (the globals of the cached lib program, the exports of a lib namespace a
    // program adds to)
    struct SymbolTable const* parent;
    SymbolEntry* entries;
    u32 count;
    u32 cap;
    u32* index; // Open addressing, entry index + 1
    u32 index_cap;
} SymbolTable;

u64 symbol_name_hash(csview name);
// Including the tables this one overlays
Symbol* symbol_table_lookup(SymbolTable const* me, csview name, u64 hash);
static inline Symbol* symbol_table_get(SymbolTable const* me, csview name) {
    return me ? symbol_table_lookup(me, name, symbol_name_hash(name)) : nullptr;
}
// Every symbol reachable by name (the name it is in the table by: `export default class C` is `default`), overlaid
// tables first, malloc'd (null if empty)
SymbolEntry* symbol_table_collect(SymbolTable const* me, u32* count);

// The AST node a declaration of a symbol was made from
typedef enum SymbolDeclKind {
    SYMBOL_DECL_NONE = 0,
    SYMBOL_DECL_VARIABLE,       // Binding: variables, parameters, destructured elements
    SYMBOL_DECL_FUNCTION,       // FunctionImpl
    SYMBOL_DECL_CLASS,          // ClassDecl
    SYMBOL_DECL_INTERFACE,      // InterfaceDecl
    SYMBOL_DECL_ENUM,           // EnumDecl
    SYMBOL_DECL_ENUM_MEMBER,    // EnumCase
    SYMBOL_DECL_TYPE_ALIAS,     // TypeDecl
    SYMBOL_DECL_TYPE_PARAMETER, // GenericItem
    SYMBOL_DECL_INFER,          // InferredType, several of the same name in a conditional type are one type parameter
    SYMBOL_DECL_MAPPED_KEY,     // InterfaceMember with a mapped key
    SYMBOL_DECL_NAMESPACE,      // NamespaceDecl
    SYMBOL_DECL_ALIAS,          // PortItem
    SYMBOL_DECL_EXPR,           // Expr: `export default <expr>`
    SYMBOL_DECL_BUILTIN,        // TypeNode const*: `number`, `globalThis`... (program_add_builtins())
} SymbolDeclKind;

typedef struct SymbolDecl {
    SymbolDeclKind kind;
    u32 flags; // What this declaration added to the symbol's flags
    void const* node;
    Span name;
    Span full;
} SymbolDecl;

typedef enum AliasState {
    ALIAS_UNRESOLVED = 0,
    ALIAS_IN_FLIGHT,
    ALIAS_DONE,
} AliasState;

typedef struct Symbol {
    Binder* binder;
    u32 flags;
    u32 decl_flags_before; // The flags before the latest declaration merged in
    csview name;
    Symbol* parent; // The container: namespace, enum, module
    SymbolTable* exports; // Namespaces, enums, modules: one table, shared by all declaration blocks
    Span name_span; // Of the first declaration
    SymbolDecl* decls; // Every declaration, in order
    u32 decl_count;
    u32 decl_cap;
    bool exported;
    // `import * as ns`: the module whose exports are the members, see make_gather_target()
    Symbol* gather_module;

    // Aliases
    PortItem* alias_item;
    Scope* alias_scope; // Where `import x = A.B` and `export { x }` resolve from
    Span alias_name; // `export default x`, which has no alias_item: the name to resolve from alias_scope
    AliasState alias_state;
    Symbol* alias_target; // Never an alias that is nothing else. A placeholder when resolution failed

    // Modules
    SourceFile* module_file;
    PortItem** export_stars;
    u32 export_star_count;
    u32 export_star_cap;
    SymbolTable* resolved_exports; // Own exports + `export *`, see module_resolved_exports()
    AliasState resolved_exports_state;

    bool is_module;
    bool is_placeholder; // Stands for something that couldn't be resolved: everything looked up in it is any
} Symbol;

// The symbol that gives the name the given meaning: itself, or for an alias its target (null if there is none).
// Placeholders mean everything
Symbol const* symbol_for_meaning(Symbol const* me, u32 meaning);
// The symbol whose exports are the members of a namespace, enum or module symbol
static inline Symbol const* symbol_members(Symbol const* me) {
    return me && my gather_module ? my gather_module : me;
}
// The first declaration of the given kind, null if there is none
SymbolDecl const* symbol_find_decl(Symbol const* me, SymbolDeclKind kind);
static inline void const* symbol_find_node(Symbol const* me, SymbolDeclKind kind) {
    SymbolDecl const* decl = symbol_find_decl(me, kind);
    return decl ? decl->node : nullptr;
}
// The type parameters of the (first) class, interface or type alias declaration, null if it has none
Generic const* symbol_type_generic(Symbol const* me);
// The full span of every declaration
VecSpan symbol_declaration_spans(Symbol const* me);
// The flags of the symbol with aliases seen through
u32 symbol_resolved_flags(Symbol const* me);
Symbol* symbol_resolve_alias(Symbol* me);
// Member of a namespace, enum or module (`export *` included), null if there is none
Symbol* symbol_get_export(Symbol const* me, csview name);
// All of them, malloc'd
SymbolEntry* symbol_collect_exports(Symbol const* me, u32* count);

typedef struct DeclProps {
    Span name;
    Span full;
    bool exported;
    SymbolDeclKind kind;
    void const* node;
    bool function_scoped; // `var`
    bool is_parameter;
    bool is_const_enum;
    bool is_instantiated; // Namespaces: has a value
} DeclProps;

typedef enum ScopeKind {
    SCOPE_BLOCK = 0, // Blocks, loops, catch, switch, type parameter lists, `infer`, mapped types, class bodies
    SCOPE_FUNCTION,
    SCOPE_STATIC_BLOCK, // `static {}` of a class: keeps its `var`s like a function
    SCOPE_GLOBAL, // The program's globals
    SCOPE_SCRIPT, // A non-module file: no locals, declarations are global
    SCOPE_GLOBAL_AUGMENTATION, // `declare global {}`, same
    SCOPE_MODULE, // A module file, the container is the module symbol
    SCOPE_NAMESPACE, // One block of a namespace, the container is the namespace symbol
    SCOPE_ENUM,
} ScopeKind;

typedef struct Scope {
    Scope* parent;
    ScopeKind kind;
    Binder* binder;
    ErrorCtx* errs;
    SymbolTable locals;
    Symbol* container; // Exported declarations go to its exports, and lookups see them
    // Ambient namespace (or declaration file) without export declarations: everything is exported
    bool export_context;
    bool ambient;
    ThisRef this_ref;
} Scope;

// Symbol pointer -> symbol pointer, open addressing
typedef struct SymbolMap {
    Symbol const** keys;
    Symbol** values;
    u32 count;
    u32 cap;
} SymbolMap;

typedef struct Binder {
    Arena arena;
    ErrorCtx* errs;
    SymbolTable globals;
    Scope* global_scope;
    Binder const* parent;
    // Symbols of the parent (the shared lib program) this program has added to -> the clone that has everything.
    // What's inside the lib AST keeps pointing at the frozen original, so the checker maps through binder_redirect()
    SymbolMap overlays;
    Symbol** symbols; // malloc'd, all of them: aliases get resolved eagerly
    u32 symbol_count;
    u32 symbol_cap;
} Binder;

Binder* binder_new(ErrorCtx* errs);
void binder_destroy(Binder* me);
// Makes the globals of `parent` (which must outlive me and not change anymore) visible, without copying them
void binder_set_parent(Binder* me, Binder const* parent);
// Resolves every alias, so that nothing writes to a symbol afterwards, and reports import conflicts
void binder_link(Binder* me);
Symbol* binder_new_symbol(Binder* me, csview name, u32 flags);
Symbol* binder_new_module_symbol(Binder* me, SourceFile* file);

Scope* scope_new(Scope* parent, ScopeKind kind);
Scope* scope_new_file(Binder* binder, SourceFile* file);
// Child scope holding the type parameters, names looked up. Returns parent itself for an empty generic
Scope* scope_new_generic(Scope* parent, Generic* generic, ErrorCtx* errs);
bool scope_has_function(Scope const* me);
// Whether `var` and function declarations of nested blocks end up here
static inline bool scope_is_var_container(Scope const* me) {
    return my kind != SCOPE_BLOCK && my kind != SCOPE_ENUM;
}

// The program's version of a symbol: the overlay if it added to a symbol of the shared lib program
Symbol const* binder_redirect_slow(Binder const* me, Symbol const* symbol);
static inline Symbol const* binder_redirect(Binder const* me, Symbol const* symbol) {
    // Nothing is ever written to a symbol of the parent, so there's no flag to test: programs that add to lib
    // symbols are rare, and only those pay for the lookup
    return symbol && my overlays.count && symbol->binder != me ? binder_redirect_slow(me, symbol) : symbol;
}

// Declares props in the scope (or the exports of its container, or the nearest function scope), merging with an
// existing symbol when tsc's rules allow it. Checks that ret was previously null
void scope_init_declaration(Scope* me, Symbol** ret, DeclProps props);
void scope_add_port(Scope* me, PortItem* item);
// `export default` of a declaration (which is declared locally under its own name) and of an expression
void scope_export_default(Scope* me, Symbol* symbol);
void scope_export_default_expr(Scope* me, Expr* expr);

// Lookup by meaning, seeing through aliases. The symbol found may be an alias, see symbol_declaration()
Symbol* scope_resolve(Scope const* me, csview name, u32 meaning);
// Same, returning the symbol that has the meaning (aliases resolved), *found is what the name itself refers to
Symbol const* scope_get_value(Scope const* me, csview name, Symbol const** found);
Symbol const* scope_get_type(Scope const* me, csview name, Symbol const** found);
Symbol const* scope_get_namespace(Scope const* me, csview name, Symbol const** found);
// What `this` is here. In a type position only a class will do
ThisRef scope_get_this(Scope const* me, bool type_else_value);
