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
typedef struct FlowBuilder FlowBuilder; // flow.h
typedef struct NamespaceDecl NamespaceDecl; // statement.h
typedef struct Error Error; // error.h

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
    // Members (not binder symbols in tttt: only ty_check_class_duplicate_members() uses these)
    SYMBOL_METHOD = 1 << 13,
    SYMBOL_GET_ACCESSOR = 1 << 15,
    SYMBOL_SET_ACCESSOR = 1 << 16,
    SYMBOL_TYPE_PARAMETER = 1 << 18,
    SYMBOL_TYPE_ALIAS = 1 << 19,
    SYMBOL_ALIAS = 1 << 21,

    SYMBOL_ENUM = SYMBOL_REGULAR_ENUM | SYMBOL_CONST_ENUM,
    GLOBAL_BLOCK_SCOPED = SYMBOL_BLOCK_SCOPED_VARIABLE | SYMBOL_CLASS | SYMBOL_ENUM, // tsc's SymbolFlags.BlockScoped
    SYMBOL_VARIABLE = SYMBOL_FUNCTION_SCOPED_VARIABLE | SYMBOL_BLOCK_SCOPED_VARIABLE,
    SYMBOL_VALUE = SYMBOL_VARIABLE | SYMBOL_PROPERTY | SYMBOL_ENUM_MEMBER | SYMBOL_FUNCTION | SYMBOL_CLASS |
        SYMBOL_ENUM | SYMBOL_VALUE_MODULE | SYMBOL_METHOD | SYMBOL_GET_ACCESSOR | SYMBOL_SET_ACCESSOR,
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
    SYMBOL_PROPERTY_EXCLUDES = 0,
    SYMBOL_METHOD_EXCLUDES = SYMBOL_VALUE & ~SYMBOL_METHOD,
    SYMBOL_GET_ACCESSOR_EXCLUDES = SYMBOL_VALUE & ~SYMBOL_SET_ACCESSOR,
    SYMBOL_SET_ACCESSOR_EXCLUDES = SYMBOL_VALUE & ~SYMBOL_GET_ACCESSOR,
} SymbolFlags;

// tsc's getExcludedSymbolFlags: what a declaration with these flags can't merge with
u32 symbol_excludes(u32 includes);

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
    SYMBOL_DECL_ASSIGNMENT,     // Expr: the value of a JavaScript assignment that declares (`exports.x = <expr>`)
    // Expr: the `f` of an assignment `f.x = v` that declares a member of f (an expando, see scope_add_expandos()). Only a
    // value of a function, class, enum or namespace (the members, like a namespace's); a variable's initializer has them
    SYMBOL_DECL_EXPANDO,
    SYMBOL_DECL_COMMONJS_MODULE, // SourceFile: the `module` of a CommonJS file, `{ exports: ... }`
    SYMBOL_DECL_BUILTIN,        // TypeNode const*: `number`, `globalThis`... (program_add_builtins())
    // Symbol const*: what `export =` names in a module a named import is from; the value is its property of the
    // import's name (tsc's symbolFromVariable in getExternalModuleMember)
    SYMBOL_DECL_EXPORT_EQUALS_PROPERTY,
} SymbolDeclKind;

// What kind of assignment a SYMBOL_DECL_ASSIGNMENT is (tsc's AssignmentDeclarationKind, as far as typing cares)
typedef enum AssignmentDeclKind : uint8_t {
    ASSIGNMENT_DECL_PROPERTY = 0, // `exports.x = v`, `f.x = v`
    ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR, // `this.x = v` in a constructor or constructor function (isDeclarationInConstructor)
    ASSIGNMENT_DECL_THIS_IN_METHOD, // `this.x = v` anywhere else
    ASSIGNMENT_DECL_PROTOTYPE_PROPERTY, // `F.prototype.x = v`
} AssignmentDeclKind;

// JSDoc modifier tags of a JavaScript declaration (tsc's getJSDocModifierFlagsNoCache)
typedef enum JsDocModifiers {
    JSDOC_MODIFIER_PRIVATE = 1 << 0,
    JSDOC_MODIFIER_PROTECTED = 1 << 1,
    JSDOC_MODIFIER_READONLY = 1 << 2,
    JSDOC_MODIFIER_OVERRIDE = 1 << 3,
} JsDocModifiers;

typedef struct SymbolDecl {
    SymbolDeclKind kind;
    AssignmentDeclKind assignment; // SYMBOL_DECL_ASSIGNMENT only
    u32 flags; // What this declaration added to the symbol's flags
    uint8_t jsdoc_modifiers; // SYMBOL_DECL_ASSIGNMENT: JsDocModifiers
    void const* node;
    // SYMBOL_DECL_ASSIGNMENT: the JSDoc `@type` of the assignment (`/** @type {T} */ this.x = v`), which is the
    // declared type (tsc's getAnnotatedTypeForAssignmentDeclaration)
    struct TypeNode const* jsdoc_type;
    Span name;
    Span full;
    // The scope (block) it was declared in, when scope_init_declaration() declared it: the symbols baseline lists a
    // block's function declarations first, see symbol_declaration_spans()
    struct Scope const* block;
} SymbolDecl;

typedef enum AliasState {
    ALIAS_UNRESOLVED = 0,
    ALIAS_IN_FLIGHT,
    ALIAS_CIRCULAR, // In flight, and its resolution reached it again (TS2303)
    ALIAS_DONE,
} AliasState;

// What only aliases have
typedef struct SymbolAlias {
    PortItem* item;
    Scope* scope; // Where `import x = A.B` and `export { x }` resolve from
    Span name; // `export default x`, which has no item: the name to resolve from scope
    AliasState state;
    // An alias that is nothing else only if that alias has a value_target. A placeholder when resolution failed
    Symbol* target;
    // `import { Request } from "m"` where m's `export =` names a value whose namespace exports `Request` without a
    // value meaning: the property `Request` of the value, which gives the import its value meaning (tsc's
    // combineValueAndTypeSymbols). `export default F` of a local type F and an exported value F: the value (tsc's
    // local symbol with an exportSymbol). Null otherwise
    Symbol* value_target;
    // An import or re-export of a name its module only exports through `export type *`: that star
    PortItem const* type_only_star;
} SymbolAlias;

// What only modules (files and ambient modules) have
typedef struct SymbolModule {
    SourceFile* file; // Null for an ambient module
    PortItem** export_stars;
    u32 export_star_count;
    u32 export_star_cap;
    SymbolTable* resolved_exports; // Own exports + `export *`, see module_resolved_exports()
    AliasState resolved_exports_state;
    // An ambient module (of a script) with an `export =` or `export default x`, where tsc checks its exports
    // (checkExportAssignment -> checkExternalModuleExports)
    bool has_export_assignment;
    // Of the resolved exports, those only an `export type *` brings (tsc's typeOnlyExportStarMap), and that star,
    // parallel to the table's entries. Null if there are none
    SymbolTable* type_only_exports;
    PortItem const** type_only_stars;
    u32 type_only_star_cap;
    // What `export =` assigns merged with the module's other exports, see module_export_equals()
    struct Symbol* merged_export_equals;
    // Every export's alias chain was resolved, as tsc does when it resolves the members of the module's type (see
    // resolve_module_type_members()): later visits see the members as resolved, not in flight
    bool type_members_resolved;
} SymbolModule;

typedef struct Symbol {
    Binder* binder;
    u32 flags;
    u32 decl_flags_before; // The flags before the latest declaration merged in
    csview name;
    Symbol* parent; // The container: namespace, enum, module
    SymbolTable* exports; // Namespaces, enums, modules: one table, shared by all declaration blocks
    // JavaScript classes and constructor functions: the instance members assignments declare (`this.x = v`,
    // `F.prototype.x = v`), tsc's Symbol.members for them. Class members declared in the class body aren't in it
    SymbolTable* members;
    Span name_span; // Of the first declaration
    SymbolDecl* decls; // Every declaration, in order
    u32 decl_count;
    u32 decl_cap;
    bool exported;
    // `import * as ns`: the module whose exports are the members, see make_gather_target()
    Symbol* gather_module;

    SymbolAlias* alias; // Every symbol with SYMBOL_ALIAS has one
    SymbolModule* module; // Every is_module symbol has one

    bool is_module;
    bool is_global_this;
    bool is_shorthand_module; // `declare module "m";`: every import from it is any
    bool is_placeholder; // Stands for something that couldn't be resolved: everything looked up in it is any
    // A placeholder for what an alias couldn't resolve to (tsc's unknownSymbol, of the error type), not for an import
    // of a shorthand ambient module (any)
    bool is_unresolved;
    // The `default` export of a module (named like its first declaration, `export default class A` is `A`)
    bool is_default_export;
    // The local `A` of `export default class A`, which has no meaning of its own: the default export (tsc's
    // exportSymbol of a local symbol)
    Symbol* export_symbol;
    // A clone of what a module's `export =` assigns, with the module's other exports added to its exports (tsc's
    // getCommonJsExportEquals: `module.exports = C; module.exports.x = 1`): the original. The checker sees the clone
    // as the original except for its value (ty_type_of_symbol()) and what is looked up in it
    struct Symbol* merged_export_equals_of;

    // Local variables and parameters (symbols one file owns), written by that file's resolve walk: what narrowing
    // inside closures depends on. `function` is the function the variable belongs to (null: top level of a file,
    // namespace, class field initializer or static block)
    FunctionImpl const* function;
    // Variables: the start node of the flow container the (first) declaration is in, null if nothing flows there
    struct FlowNode const* flow_start;
    bool is_local; // Declared in the locals of a scope: not a global, not an export
    bool is_const; // A `const` variable
    bool is_parameter;
    bool is_ambient; // Declared in an ambient context: never "used before assigned"
    bool is_assigned; // Somewhere other than its declaration
    bool is_definitely_assigned; // By `=`, a pattern or a for-in/of rather than only `+=` or `++`
    bool is_assigned_in_closure; // In another function than the one it belongs to
    // Where the last assignment ends: a closure made after that sees the narrowed type (tsc's lastAssignmentPos).
    // Null: not assigned. Assignments in nested functions or loops count as "anywhere" (the end of the file)
    char const* last_assignment_end;
    // tsc's isReferenced: the meanings the name was used with, see unused_mark_referenced(). Written by the resolve
    // walk of the file that uses it, only under noUnusedLocals/noUnusedParameters
    u32 referenced;
} Symbol;

// The symbol that gives the name the given meaning: itself, or for an alias its target (null if there is none).
// Placeholders mean everything
Symbol const* symbol_for_meaning(Symbol const* me, u32 meaning);
/// tsc's isPrimitiveTypeName: the type keywords that are no symbol (`any`, `string`, `number`, `boolean`, `never`,
/// `unknown`)
bool is_primitive_type_name(csview name);
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
// The function a JavaScript declaration declares: a function declaration, or a variable or assignment whose value is a
// function expression. Null otherwise
FunctionImpl const* symbol_decl_js_function(SymbolDecl const* decl);
Generic const* symbol_type_generic(Symbol const* me);
OuterGenerics const* symbol_outer_generics(Symbol const* me);
// tsc's getLocalSymbolForExportDefault: the local name of a module's `default` export (that of its first named
// declaration), empty if it has none
csview symbol_default_export_local_name(Symbol const* me);
// Where an error about the declaration goes: its name, the first token of a nameless one
Span symbol_decl_error_span(SymbolDecl const* decl);
// The full span of every declaration
VecSpan symbol_declaration_spans(Symbol const* me);
// The flags of the symbol with aliases seen through
u32 symbol_resolved_flags(Symbol const* me);
// tsc's checkTypeNameIsReserved: `any`, `string`... can't name a type
bool type_name_is_reserved(csview name);
Symbol* symbol_resolve_alias(Symbol* me);
// tsc's getTypeOnlyAliasDeclaration(): the nearest `import type`/`export type` (or `type` specifier) along the chain of
// aliases that ends in the symbol, if any. What it names can't be used as a value
PortItem const* symbol_type_only_declaration(Symbol const* me);
// Member of a namespace, enum or module (`export *` included), null if there is none
Symbol* symbol_get_export(Symbol const* me, csview name);
// The `export type *` that is the only way the module exports that name (tsc's typeOnlyExportStarMap), if any
PortItem const* module_type_only_star(Symbol* module, csview name);
// The table of them (null if there are none)
SymbolTable const* symbol_exports_table(Symbol const* me);
// All of them, malloc'd
SymbolEntry* symbol_collect_exports(Symbol const* me, u32* count);

typedef struct DeclProps {
    Span name;
    Span full;
    bool exported;
    // `export default class A`: declared in the module's exports as `default` (merging with the other default exports
    // like tsc's), and locally as `A` if it has a name
    bool default_export;
    SymbolDeclKind kind;
    void const* node;
    bool function_scoped; // `var`
    bool is_parameter;
    bool is_const; // `const` variable
    bool is_ambient; // `declare`
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

typedef enum NoSuper : uint8_t {
    NO_SUPER_NONE,
    NO_SUPER_COMPUTED_NAME, // Of a class member
    NO_SUPER_LITERAL_COMPUTED_NAME, // Of an object literal member: only picks the error message
    NO_SUPER_DECORATOR,
    NO_SUPER_CONSTRUCTOR_ARGS,
} NoSuper;

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
    // The resolve walk: the builder of the control flow container this scope is in (inherited by child scopes). Only
    // valid during the walk
    FlowBuilder* flow;
    FunctionImpl* function; // The function this scope is in, null outside of functions
    // The scope of a function expression's name, which is in that function's await/yield context (tsc's
    // parseFunctionExpression)
    FunctionImpl const* expression_name_of;
    // A computed member name, a member decorator or a constructor's parameters: `super` can't be used, tsc's
    // isLegalUsageOfSuperExpression (see resolve_super())
    NoSuper no_super;
    // The scope of a decorator of this class (for a member decorator, no_super is NO_SUPER_DECORATOR instead): an ES
    // decorator runs before the class is initialized (tsc's isBlockScopedNameDeclaredBeforeUse)
    struct ClassDecl const* decorator_of;
    // The file the scope is in (null for the global scope)
    SourceFile* file;
    // The parameter list of a function: from ES2015 on the variables its body declares aren't visible there (tsc's
    // useOuterVariableScopeInParameter), although they share the function's scope
    bool parameters;
    // Unless the downlevel emit moves the parameters into the body (tsc's useOuterVariableScopeInParameter)
    bool parameters_see_body;
    // The type of a parameter written in the function's JSDoc (`@param {typeof x} x`): the function's parameters and
    // `var`s aren't values there (tsc's resolveNameHelper: lastLocation is the JSDoc, not a parameter)
    bool jsdoc_parameter_type;
    // The inside of the declaration of this symbol (a function, class, interface, type alias, enum or namespace
    // declaration): a use of it from here isn't a reference (tsc's isSelfReferenceLocation)
    Symbol const* self;
    // Strict mode code (tsc's inStrictMode): a module, alwaysStrict, a "use strict" prologue, a class body
    bool strict;
    // The statements declared in this scope (block_add_names()), null if none were
    struct Block const* block;
    // The type and initializer of an instance field of a class with a constructor, when fields are initialized in the
    // constructor (not standard class fields): the value names the constructor declares, which the field can't use
    // (tsc's propertyWithInvalidInitializer), see scope_invalid_initializer_field()
    struct ClassItem const* instance_field;
    Span const* constructor_locals;
    u32 constructor_local_count;
    // The type parameter lists this scope is inside of (inherited by child scopes), null at the top level
    OuterGenerics const* generics;
    // A static field of a class with legacy decorators: no `this` in its initializer (TS2816)
    bool decorated_static_field;
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
    Symbol* global_this;
    Binder const* parent;
    // Symbols of the parent (the shared lib program) this program has added to -> the clone that has everything.
    // What's inside the lib AST keeps pointing at the frozen original, so the checker maps through binder_redirect()
    SymbolMap overlays;
    // `declare module "m"`, by name without quotes. Patterns ("*.css") are in the table like the others and listed
    // in pattern_modules too
    SymbolTable ambient_modules;
    Symbol** pattern_modules; // malloc'd
    u32 pattern_module_count;
    u32 pattern_module_cap;
    Symbol** symbols; // malloc'd, all of them: aliases get resolved eagerly
    u32 symbol_count;
    u32 symbol_cap;
    // `declare module "m" {}` blocks in modules, in declaration order: each declares into a symbol of its own, which
    // binder_link() merges into the module it names (tsc's mergeModuleAugmentation)
    NamespaceDecl** augmentations; // malloc'd
    u32 augmentation_count;
    u32 augmentation_cap;
    // What `require` is in `require("m")` in a JavaScript file where nothing else is called that (tsc's requireSymbol:
    // any). Made when first needed
    Symbol* require_symbol;
    // Set while the program is typechecked: nothing makes symbols anymore (several checkers share them)
    bool frozen;
    u32 suggestion_count; // Names not found so far: only the first ones get spelling suggestions (tsc's suggestionCount)
    // Values not found in class members, reported once types are known (scope_report_missing_prefixes())
    struct PendingNotFound* pending_not_found; // malloc'd
    u32 pending_not_found_count;
    u32 pending_not_found_cap;
    // Globals JavaScript expandos of another file declared or added to (`Outer.Inner = v` where the file doesn't
    // declare Outer), for binder_link_global_expandos()
    Symbol** global_expandos; // malloc'd
    u32 global_expando_count;
    u32 global_expando_cap;
} Binder;

Binder* binder_new(ErrorCtx* errs);
void binder_destroy(Binder* me);
// Makes the globals of `parent` (which must outlive me and not change anymore) visible, without copying them
void binder_set_parent(Binder* me, Binder const* parent);
// Merges module augmentations, resolves every alias, so that nothing writes to a symbol afterwards, and reports
// import conflicts
void binder_link(Binder* me);
// The aliases the resolve walk declared after binder_link() (`import I = M` in a block): resolved at the end of name
// resolution, so the checkers (which share them) never write to them
void binder_resolve_remaining_aliases(Binder* me);
// Once every file's names are declared: the initializers of the variables global expandos went to get their members
// (the variable may be declared by a file bound after the expando's)
void binder_link_global_expandos(Binder* me);
// A `declare module "m" {}` in a module, whose declarations binder_link() merges into "m"
void binder_add_augmentation(Binder* me, NamespaceDecl* decl);
Symbol* binder_new_symbol(Binder* me, csview name, u32 flags);
Symbol* binder_new_module_symbol(Binder* me, SourceFile* file);
// `globalThis`: a namespace whose exports are the globals
void binder_declare_global_this(Binder* me);
// The ambient module of that name (no quotes), made if it doesn't exist yet: every `declare module "m"` block and
// every augmentation of it (binder_link()) declare into the same symbol
Symbol* binder_ambient_module(Binder* me, csview name, Span name_span);
// A `declare module "m" {}` block of a script is a declaration of the ambient module
void binder_add_module_declaration(Symbol* module, NamespaceDecl const* decl);
// The module a string names: an ambient module, the file the program found for it if that is a module, a pattern
// ambient module. Null if there is none (module_ref_is_missing() tells "not found" from "not a module")
Symbol* binder_module_of_ref(Binder const* me, ModuleRef* ref);
// What importing the module gives: the target of its `export =` if it has one (merged with the module's other
// exports), else itself
Symbol* module_export_equals(Symbol* module);
// How tsc names a module in a message: an ambient module by its declared name, a file by the specifier it was
// imported with (TS2305, ref may be null otherwise) or by its path without the extension. With the quotes
cstr module_name_for_error(Symbol const* module, ModuleRef const* ref, bool by_specifier);
cstr symbol_fully_qualified_name(Symbol const* symbol);

Scope* scope_new(Scope* parent, ScopeKind kind);
Scope* scope_new_file(Binder* binder, SourceFile* file);
// A scope for the inside of the declaration of `self` (see Scope.self), ambient if it is `declare`d. Only made when
// unused names are reported or it is `declare`d, returns parent otherwise
// tsc's checkExportsOnMergedDeclarations: a name the block (spanning `block`) declares both exported and locally has
// to be so in different declaration spaces (TS2395)
void scope_check_export_merges(Scope* me, Span block);
// tsc's checkExternalModuleExports: a module can export a name only once (TS2323), except for merges
void scope_check_duplicate_exports(Scope* me);
// The same for the ambient modules that have an export assignment, once every file is declared
void binder_check_ambient_module_exports(Binder* me, ErrorCtx* errs);
// TS2308: two `export *` of a module that export different things by the same name (after binder_link())
void binder_check_export_star_conflicts(Binder* me, ErrorCtx* errs);
// Whether a block starts with a "use strict" directive
bool block_has_use_strict(struct Block const* block);
// tsc's checkStrictModeIdentifier: in strict mode code the future reserved words aren't names (TS1212/TS1213/TS1214)
void scope_check_strict_identifier(Scope const* me, Span name, bool in_type);
// tsc's checkStrictModeEvalOrArguments: `eval` and `arguments` can't be declared or assigned in strict mode code
void scope_check_strict_eval_or_arguments(Scope const* me, Span name);
// tsc's checkStrictModeFunctionDeclaration: below ES2015, no function declarations in the blocks of strict mode code
void scope_check_strict_block_function(Scope const* me, Span name);
Scope* scope_new_self(Scope* parent, Symbol const* self, bool declared);
// Child scope holding the type parameters, names looked up. Returns parent itself for an empty generic
Scope* scope_new_generic(Scope* parent, Generic* generic, Span owner, Span owner_class, ErrorCtx* errs);
// Of a declaration (`node`) of a class or interface `owner`: a type parameter named like one of an earlier declaration
// of it is the same symbol (tsc declares them in the merged symbol's members)
Scope* scope_new_generic_merged(
    Scope* parent, Generic* generic, Symbol const* owner, void const* node, Span owner_name, ErrorCtx* errs
);
bool scope_has_function(Scope const* me);
// Whether `var` and function declarations of nested blocks end up here
static inline bool scope_is_var_container(Scope const* me) {
    return my kind != SCOPE_BLOCK && my kind != SCOPE_ENUM;
}

// The program's version of a symbol: the overlay if it added to a symbol of the shared lib program. The clone
// module_export_equals() makes is its original
Symbol const* binder_redirect_slow(Binder const* me, Symbol const* symbol);
static inline Symbol const* binder_redirect(Binder const* me, Symbol const* symbol) {
    if (symbol && symbol->merged_export_equals_of) symbol = symbol->merged_export_equals_of;
    // Nothing is ever written to a symbol of the parent, so there's no flag to test: programs that add to lib
    // symbols are rare, and only those pay for the lookup
    if (!symbol || symbol->binder == me) return symbol;
    if (symbol->is_global_this) return my global_this; // The lib's `typeof globalThis` has this program's globals
    return my overlays.count ? binder_redirect_slow(me, symbol) : symbol;
}

// Declares props in the scope (or the exports of its container, or the nearest function scope), merging with an
// existing symbol when tsc's rules allow it. Checks that ret was previously null
void scope_init_declaration(Scope* me, Symbol** ret, DeclProps props);
void scope_add_port(Scope* me, PortItem* item);
// `export default <expr>`, export_equals: `export = x`, under the name `export=` like in tsc
void scope_export_default_expr(Scope* me, Expr* expr, Span statement, bool export_equals);
// An assignment in a CommonJS file: `exports.x = v`, `module.exports.x = v` and `module.exports = v` declare exports
// (tsc's bindExportsPropertyAssignment, bindModuleExportsAssignment). Anything else is ignored
void scope_add_commonjs_export(Scope* me, Expr* assignment, Span statement);
// The `module` of a CommonJS file that mentions `module.exports` (tsc declares it at the first such access)
void scope_add_commonjs_module(Scope* me);
// The expandos of the statements of a block (declared in `me`): `f.x = v` where f is a function declared in the block
// or its function (or file), or a variable initialized with a function (JavaScript: also a class or `{}`), declares x
// in f's exports (tsc's bindSpecialPropertyAssignment)
void scope_add_expandos(Scope* me, struct Block const* block);
// An assignment `f.x = v` nested in an expression (`(f.x = 1)`, `if (f.x = 1)`), which tsc's binder binds like any
// other: called by the resolve walk for the ones scope_add_expandos() didn't see
void scope_add_nested_expando(Scope* me, struct BinaryOpExpr* op);
// The expression a declaration initialized (or a member assigned) with `init` is an expando container of (tsc's
// getExpandoInitializer/getDefaultedExpandoInitializer): a function or arrow function, in JavaScript also a class
// expression or `{}`, possibly defaulted (`var ns = ns || {}`: `name` is the declared name, invalid for a member)
Expr* expando_initializer(Expr* init, Span name, bool js);
// JavaScript `this.x = v` (`op`, whose left side was just resolved): declares x as an instance member of the class
// `this` is of (a static member in a static method), or of the constructor function it is in (tsc's
// bindThisPropertyAssignment)
// TypeScript `this.x = v` in a constructor or static block: marks the field it types (ClassItem.field.ts_assigned)
void scope_note_ts_this_assignment(Scope const* me, struct BinaryOpExpr const* op);
void scope_add_this_property(Scope* me, struct BinaryOpExpr* op);
// JavaScript `/** @type {T} */ this.x;` (tsc's isSpecialPropertyDeclaration), after `this` is resolved
void scope_add_this_declaration(Scope* me, struct Expr* access, struct TypeNode const* jsdoc_type);
// `require` in a `require("m")` of a JavaScript file that doesn't declare the name
Symbol* binder_require_symbol(Binder* me);

// Whether the scope's own tables (its locals, its container's exports) have the symbol
bool scope_declares_symbol(Scope const* me, Symbol const* symbol);
// Lookup by meaning, seeing through aliases. The symbol found may be an alias, see symbol_declaration()
// tsc's getSpellingSuggestion: the candidate name closest to `name`, if any is close enough. `names` are `count`
// strings, returns the index of the best, or -1
isize spelling_suggestion(csview name, csview const* names, isize count);
// tsc's reportNonExportedMember: `import { x }` of a module without an export x (TS2305), which declares x locally
// (TS2459/TS2460), or whose `export =` is x (TS2595-TS2598, TS2616/TS2617)
void scope_report_non_exported_member(PortItem const* item, Symbol const* module, ErrorCtx* errs);
// TS6203 "was also declared here" (TS6204 "and here" from the second on, at most 5) on the duplicate error at `at`,
// pointing at `other` (tsc's addDuplicateDeclarationError)
void add_duplicate_related(Error* error, Span at, Span other);
// A name used with `meaning` wasn't found: TS2304, or TS2552 with the closest name in scope (tsc's
// onFailedToResolveSymbol with getSuggestedSymbolForNonexistentSymbol). `shorthand`: the value of `{ a }` (TS18004)
void scope_report_not_found(Scope const* me, Span name, u32 meaning, bool shorthand, ErrorCtx* errs);
// Its TS2552 half: `name`, which `at` needs, is misspelled (a spelling suggestion was found and reported)
bool scope_report_misspelled(Scope const* me, Span at, csview name, u32 meaning, ErrorCtx* errs);
// The same for a value used in an expression, which in a class may be a member missing `this.` (TS2663/TS2662): that
// waits for the checker
void scope_report_value_not_found(Scope const* me, Span name, bool shorthand, ErrorCtx* errs);
// Reports what scope_report_value_not_found() deferred, with the checker current
void scope_report_missing_prefixes(Binder* me);
Symbol* scope_resolve(Scope const* me, csview name, u32 meaning);
// A value `name` used here, which resolved to `found` (maybe null), passes an instance field on the way whose
// constructor declares a value of that name (TS2301): that field, else null
struct ClassItem const* scope_invalid_initializer_field(Scope const* me, csview name, Symbol const* found);
// Reports TS2301 (TS2844 in the field's type) for such a use
void scope_report_invalid_initializer_reference(struct ClassItem const* field, Span name, ErrorCtx* errs);
// Same, returning the symbol that has the meaning (aliases resolved), *found is what the name itself refers to
Symbol const* scope_get_value(Scope const* me, csview name, Symbol const** found);
Symbol const* scope_get_type(Scope const* me, csview name, Symbol const** found);
Symbol const* scope_get_namespace(Scope const* me, csview name, Symbol const** found);
// What `this` is here. In a type position only a class will do
ThisRef scope_get_this(Scope const* me, bool type_else_value);
// The innermost class body this scope is in (through functions), null if none
struct ClassDecl const* scope_enclosing_class(Scope const* me);
