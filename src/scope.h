#pragma once

#include "type.h"
#include "expr.h"
#include "arena.h"
#include "error.h"

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
    // (TypeScript 7: a property merges with properties and accessors, duplicates are the checker's
    // checkObjectTypeForDuplicateDeclarations)
    SYMBOL_PROPERTY_EXCLUDES = SYMBOL_VALUE & ~(SYMBOL_PROPERTY | SYMBOL_GET_ACCESSOR | SYMBOL_SET_ACCESSOR),
    SYMBOL_METHOD_EXCLUDES = SYMBOL_VALUE & ~SYMBOL_METHOD,
    SYMBOL_GET_ACCESSOR_EXCLUDES = SYMBOL_VALUE & ~(SYMBOL_SET_ACCESSOR | SYMBOL_PROPERTY),
    SYMBOL_SET_ACCESSOR_EXCLUDES = SYMBOL_VALUE & ~(SYMBOL_GET_ACCESSOR | SYMBOL_PROPERTY),
} SymbolFlags;

// tsc's getExcludedSymbolFlags: what a declaration with these flags can't merge with
u32 symbol_excludes(u32 includes);

// (24 bytes: the name's length and the low half of its hash share a word)
typedef struct SymbolEntry {
    char const* name_buf; // Points into the source (or a literal), never owned
    u32 name_len;
    u32 hash; // symbol_name_hash()'s low half: tables compare and index by that
    Symbol* symbol;
} SymbolEntry;

static inline csview symbol_entry_name(SymbolEntry const* me) {
    return (csview){ .buf = my name_buf, .size = my name_len };
}
static inline SymbolEntry symbol_entry_make(csview name, u64 hash, Symbol* symbol) {
    return (SymbolEntry){ .name_buf = name.buf, .name_len = (u32)name.size, .hash = (u32)hash, .symbol = symbol };
}

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
typedef enum SymbolDeclKind : uint8_t {
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
    SYMBOL_DECL_COMMONJS_EXPORTS, // SourceFile: the `exports` of a CommonJS file, what the module exports
    SYMBOL_DECL_BUILTIN,        // TypeNode const*: `number`, `globalThis`... (program_add_builtins())
    // Symbol const*: what `export =` names in a module a named import is from; the value is its property of the
    // import's name (tsc's symbolFromVariable in getExternalModuleMember)
    SYMBOL_DECL_EXPORT_EQUALS_PROPERTY,
    // PortItem const*: what the default binding of a source phase import (`import source s from "m"`) stands for, a
    // variable of the global AbstractModuleSource type (tsgo's getTargetOfImportClause)
    SYMBOL_DECL_SOURCE_PHASE,
} SymbolDeclKind;

// What kind of assignment a SYMBOL_DECL_ASSIGNMENT is (tsc's AssignmentDeclarationKind, as far as typing cares)
typedef enum AssignmentDeclKind : uint8_t {
    ASSIGNMENT_DECL_PROPERTY = 0, // `exports.x = v`, `f.x = v`
    ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR, // `this.x = v` in a constructor or constructor function (isDeclarationInConstructor)
    ASSIGNMENT_DECL_THIS_IN_METHOD, // `this.x = v` anywhere else
    // `Object.defineProperty(o, "x", descriptor)` (of `exports`, `F.prototype` or a container o): the declaration's node
    // is the call, the type is the descriptor's value, getter or setter (tsc's getInitializerTypeFromAssignmentDeclaration)
    ASSIGNMENT_DECL_DEFINE_PROPERTY,
} AssignmentDeclKind;

// JSDoc modifier tags of a JavaScript declaration (tsc's getJSDocModifierFlagsNoCache)
typedef enum JsDocModifiers {
    JSDOC_MODIFIER_PRIVATE = 1 << 0,
    JSDOC_MODIFIER_PROTECTED = 1 << 1,
    JSDOC_MODIFIER_READONLY = 1 << 2,
    JSDOC_MODIFIER_OVERRIDE = 1 << 3,
} JsDocModifiers;

// What only JavaScript assignment declarations (and `export default`/`export =` with a JSDoc type) have
typedef struct SymbolDeclJs {
    // SYMBOL_DECL_ASSIGNMENT: the JSDoc `@type` of the assignment (`/** @type {T} */ this.x = v`), which is the
    // declared type (tsc's getAnnotatedTypeForAssignmentDeclaration)
    struct TypeNode const* jsdoc_type;
    // SYMBOL_DECL_ASSIGNMENT of a dot access (`f.x = v`, `this.x = v`): the control flow container (FlowBuilder.container)
    // the assignment is in, set by the resolve walk
    void const* flow_container;
    // SYMBOL_DECL_ALIAS of a CommonJS export (`exports.x = a`): the value, whose type joins the others' when the export
    // isn't only aliases (tsgo's getWidenedTypeForAssignmentDeclaration of a variable with Alias declarations)
    struct Expr const* commonjs_value;
} SymbolDeclJs;

typedef struct SymbolDecl {
    SymbolDeclKind kind;
    AssignmentDeclKind assignment; // SYMBOL_DECL_ASSIGNMENT only
    uint8_t jsdoc_modifiers; // SYMBOL_DECL_ASSIGNMENT: JsDocModifiers
    // A `var` (or sloppy mode function) hoisted out of the nested statement it is written in: `block` is where it was
    // hoisted to, not the statement list it is in
    bool hoisted : 1;
    // Written with `export` / `declare` (a declaration file's export context: tsc's getEffectiveDeclarationFlags)
    bool written_export : 1;
    bool written_declare : 1;
    u32 flags; // What this declaration added to the symbol's flags
    void const* node;
    // Every SYMBOL_DECL_ASSIGNMENT has one, other kinds only with a JSDoc type (symbol_decl_js()). Null otherwise
    SymbolDeclJs* js;
    Span name;
    Span full;
    // The scope (block) it was declared in, when scope_init_declaration() declared it: the symbols baseline lists a
    // block's function declarations first, see symbol_declaration_spans()
    struct Scope const* block;
} SymbolDecl;

static inline struct TypeNode const* symbol_decl_jsdoc_type(SymbolDecl const* me) {
    return my js ? my js->jsdoc_type : nullptr;
}

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
    // ... when the type half is an interface: tsc's combined symbol, an interface of its own with the interface's
    // declarations (getDeclaredTypeOfSymbol makes a second interface type, with a `this` of its own). Which one a type
    // reference means is up to the checker: the combined one only if the value has the property (lower_identifier())
    Symbol* combined_type;
    // An import or re-export of a name its module only exports through `export type *`: that star
    PortItem const* type_only_star;
    // JavaScript `exports.C = class {}`: the class expression, whose symbol is the target (tsc's isAliasableExpression)
    struct ClassDecl* class_expr;
    // `export = a.b.c` / `export default a.b.c` (tsc's exportAssignmentIsAlias: an entity name expression): resolved
    // like tsc's resolveEntityName with every meaning, see resolve_entity_alias()
    struct Expr* entity;
    // An import of the name an exported value of its module also declares (`import x from "m"; export const x = 1`):
    // that value, which a value use of the name is (tsc's merged local symbol, getExportSymbolOfValueSymbolIfExported)
    Symbol* export_value;
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
    // Every export's alias chain was resolved, as tsc does when it resolves the members of the module's type (see
    // resolve_module_type_members()): later visits see the members as resolved, not in flight
    bool type_members_resolved;
    // A pattern ambient module's `with { type: "css" }` (a type literal, TYPE_INTERFACE, with members), which imports
    // must have to be it (tsgo's getTypeOfModuleImportAttributes). Null for none (or `with {}`)
    struct TypeNode const* attributes;
} SymbolModule;

// What few symbols have (namespace imports, export defaults, JavaScript members...): behind Symbol.rare, read through
// symbol_rare() (a zeroed one when absent), written through symbol_rare_mut() while binding (not once frozen)
typedef struct SymbolRare {
    // JavaScript classes and constructor functions: the instance members assignments declare (`this.x = v`,
    // `F.prototype.x = v`), tsc's Symbol.members for them. Class members declared in the class body aren't in it
    SymbolTable* members;
    // `import * as ns`: the module whose exports are the members, see make_gather_target()
    struct Symbol* gather_module;
    // `import * as ns` of a CommonJS module into an ES module in Node.js: a `default` of its type (not of its exports)
    // that is the module, an alias of it. Null otherwise
    struct Symbol* synthetic_default;
    // The local `A` of `export default class A`, which has no meaning of its own: the default export (tsc's
    // exportSymbol of a local symbol)
    struct Symbol* export_symbol;
    // JavaScript members `this.x = v` declares in a class constructor: the first such `this.x` and the constructor,
    // where the flow query of tsc's getFlowTypeInConstructor goes (lower.c's assignment_symbol_type)
    struct Expr const* constructor_assigned;
    FunctionImpl const* declaring_constructor;
    // A JavaScript class's member `this[k] = v` declares with an entity name k (tsc's late bound assignment
    // declarations): its name is what k's type names, known to the checker only. In Symbol.members as `[k]`
    struct Expr const* late_bound_key;
    // An alias (`export { A }` in a namespace) that a cross-file merge went through (merge_into_exports()): tsgo's
    // mergeSymbol resolved it first, which keeps the unmerged target as the alias's target, and put a clone of the
    // target in the table, where the alias no longer is (no name reaches it). `detached_target_flags`: the target's
    // flags before the merge
    u32 detached_target_flags;
    bool merge_detached;
    // An expando of a `{}` container (`ns.a = v` of `var ns = {}`): a member of the object literal's symbol in tsgo,
    // which has no name. Here it shares the host's exports, where the namespace of a dotted JSDoc typedef (`@typedef
    // {T} ns.a.B`) may have declared the name already: then they're one symbol, whose value is the expando's alone (its
    // property has the expando's declarations, unqualified: namespace_export_prop())
    bool literal_member;
    // A module whose `export =` names a namespace import that tsgo resolves to a clone (resolveESModuleSymbol's
    // cloneTypeAsModuleType: the imported value has signatures or a `default`), augmented: the clone the augmentations
    // merge into (module_export_equals() answers it), also set on that namespace import (which is the clone to tsgo:
    // a name it makes doesn't mean what it was cloned from). On the clone: the namespace import, whose type it has,
    // and the symbol it was cloned from (its declarations are copied when the first augmentation adds its own)
    struct Symbol* export_equals_clone;
    struct Symbol* clone_of_import;
    struct Symbol* clone_source;
} SymbolRare;

// PERF: ~80k of them on nest/webpack: keep it small (what few symbols have goes to SymbolRare)
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
    SymbolAlias* alias; // Every symbol with SYMBOL_ALIAS has one
    SymbolModule* module; // Every is_module symbol has one
    SymbolRare* rare; // Null: all zero
    // Local variables and parameters (symbols one file owns), written by that file's resolve walk: what narrowing
    // inside closures depends on. `function` is the function the variable belongs to (null: top level of a file,
    // namespace, class field initializer or static block)
    FunctionImpl const* function;
    // Variables: the start node of the flow container the (first) declaration is in, null if nothing flows there
    struct FlowNode const* flow_start;
    // Where the last assignment ends: a closure made after that sees the narrowed type (tsc's lastAssignmentPos).
    // Null: not assigned. Assignments in nested functions or loops count as "anywhere" (the end of the file)
    char const* last_assignment_end;
    // tsc's isReferenced: the meanings the name was used with, see unused_mark_referenced(). Written by the resolve
    // walk of the file that uses it, only under noUnusedLocals/noUnusedParameters
    u32 referenced;
    bool exported;
    bool is_module;
    // The symbol of a module augmentation's block (`declare module "./a" {}` in a module), which binder_link() merges
    // into the module: declares like a module (`export default`)
    bool is_augmentation;
    bool is_global_this;
    bool is_shorthand_module; // `declare module "m";`: every import from it is any
    bool is_placeholder; // Stands for something that couldn't be resolved: everything looked up in it is any
    // A placeholder for what an alias couldn't resolve to (tsc's unknownSymbol, of the error type), not for an import
    // of a shorthand ambient module (any)
    bool is_unresolved;
    // The `default` export of a module (named like its first declaration, `export default class A` is `A`)
    bool is_default_export;
    bool is_local; // Declared in the locals of a scope: not a global, not an export
    bool is_const; // A `const` variable
    bool is_parameter;
    bool is_ambient; // Declared in an ambient context: never "used before assigned"
    bool is_assigned; // Somewhere other than its declaration
    bool is_definitely_assigned; // By `=`, a pattern or a for-in/of rather than only `+=` or `++`
    bool is_assigned_in_closure; // In another function than the one it belongs to
    // several_types.c's declared_type_params(), once the declarations are all there (checking): 0 not known yet, 1 the
    // first declaration's type parameters, 2 a merge by name of the class and interface declarations' (atomic: checkers)
    uint8_t type_params_merge;
    // The same: how many type parameters a reference may give, min | max << 6, plus 1 << 12 (0: not known yet)
    uint16_t type_args_arity;
} Symbol;

extern SymbolRare const symbol_rare_none;
static inline SymbolRare const* symbol_rare(Symbol const* me) {
    return my rare ? my rare : &symbol_rare_none;
}
SymbolRare* symbol_rare_new(Symbol* me);
static inline SymbolRare* symbol_rare_mut(Symbol* me) {
    return my rare ? my rare : symbol_rare_new(me);
}

// The symbol that gives the name the given meaning: itself, or for an alias its target (null if there is none).
// Placeholders mean everything
Symbol const* symbol_for_meaning(Symbol const* me, u32 meaning);
// The export `name` of a namespace (as symbol_for_meaning(..., SYMBOL_NAMESPACE) gives it), else of what it resolves
// to when it is an alias with a namespace meaning of its own (tsc's resolveQualifiedName)
Symbol const* symbol_namespace_member(Symbol const* me, csview name);
// A symbol that isn't a module but that a module augmentation (`declare module "./a" {}`) declares too: what an
// `export =` assigns (tsc's hasNonGlobalAugmentationExternalModuleSymbol of one of its declarations)
bool symbol_is_augmented_export_equals(Symbol const* me);
/// tsc's isPrimitiveTypeName: the type keywords that are no symbol (`any`, `string`, `number`, `boolean`, `never`,
/// `unknown`)
bool is_primitive_type_name(csview name);
// A const enum only its declaring file can inline (TS2748 under isolatedModules): declared ambient, or in a
// referenced project's source whose `.d.ts` tsc -b would read (SourceFile.reference_const_enums_ambient)
bool symbol_const_enum_is_ambient(Symbol const* me);
// The symbol whose exports are the members of a namespace, enum or module symbol
static inline Symbol const* symbol_members(Symbol const* me) {
    return me && symbol_rare(me)->gather_module ? symbol_rare(me)->gather_module : me;
}
// The first declaration of the given kind, null if there is none
SymbolDecl const* symbol_find_decl(Symbol const* me, SymbolDeclKind kind);
static inline void const* symbol_find_node(Symbol const* me, SymbolDeclKind kind) {
    SymbolDecl const* decl = symbol_find_decl(me, kind);
    return decl ? decl->node : nullptr;
}
// The type parameters of the (first) class, interface or type alias declaration, null if it has none
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
bool symbol_is_builtin_type(Symbol const* symbol); // `number`, `any`...: not a symbol to tsc
Symbol* symbol_resolve_alias(Symbol* me);
// Frees this thread's alias resolution stack (thread-local, lazily remade): before a thread that resolved aliases exits
void scope_release_thread(void);
// tsc's getTypeOnlyAliasDeclaration(): the nearest `import type`/`export type` (or `type` specifier) along the chain of
// aliases that ends in the symbol, if any. What it names can't be used as a value
PortItem const* symbol_type_only_declaration(Symbol const* me);
// Also what the type-only declaration names, whose meaning decides whether a value use is an error (null if `me` itself)
PortItem const* symbol_type_only_declaration_naming(Symbol const* me, Symbol const** named);
// tsc's addTypeOnlyDeclarationRelatedInfo: TS1376/TS1377 on the type-only import or export (expr.c)
void port_item_add_type_only_related(Error* error, PortItem const* item, csview name);
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
    // What the symbol is named and keyed by when that isn't name's text (an enum member's canonical name)
    csview symbol_name;
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
    bool is_ambient; // `declare`, or in a declaration file
    bool declare_keyword; // An explicit `declare`
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
    // ... of an object literal method or accessor: a super call in it isn't the constructor's (tsc's findFirstSuperCall
    // stops at function-like nodes, their names included)
    NO_SUPER_LITERAL_METHOD_COMPUTED_NAME,
    NO_SUPER_DECORATOR,
    NO_SUPER_CONSTRUCTOR_ARGS,
} NoSuper;

// What few scopes have (functions, classes, decorators, computed names...): behind Scope.rare, read through
// scope_rare() (a zeroed one when absent), written through scope_rare_mut()
typedef struct ScopeRare {
    ThisRef this_ref;
    // The scope of a function expression's name, which is in that function's await/yield context (tsc's
    // parseFunctionExpression)
    FunctionImpl const* expression_name_of;
    // The scope of a for-in loop, once the walk is in its body (tsc's isForInVariableForNumericPropertyNames)
    struct ForEachLoopStatement const* for_in_body;
    // A computed member name of a class or interface: the type parameters of that class or interface, which it can't
    // reference (TS2467, tsc's resolveNameHelper at a ComputedPropertyName)
    struct Generic const* computed_name_of;
    // The base class expression of a class (not its type arguments): the type parameters of that class, which it can't
    // reference (TS2562, tsc's resolveNameHelper at an ExpressionWithTypeArguments)
    struct Generic const* base_expression_of;
    // The scope of a type literal whose only member is a property signature with a computed name: that name (an
    // identifier naming a type there may have meant a mapped type, tsc's maybeMappedType)
    struct Expr const* lone_computed_key;
    // A parameter decorator's scope: the function whose parameter it decorates, what tsc's getContainingFunction finds
    // for the decorator (TS1356 suggests making that one async)
    struct FunctionImpl const* decorated_function;
    // The scope of a decorator of this class (for a member decorator, no_super is NO_SUPER_DECORATOR instead): an ES
    // decorator runs before the class is initialized (tsc's isBlockScopedNameDeclaredBeforeUse)
    struct ClassDecl const* decorator_of;
    // The parameter list of a function: from ES2015 on the variables its body declares aren't visible there (tsc's
    // useOuterVariableScopeInParameter), although they share the function's scope
    // A scope of a function's type parameters (scope_new_generic()): the function's scope, which has its parameters
    // and the top level of its body (one table in tsc: see baseline_symbol_declaration_spans())
    struct Scope* type_params_function;
    // The type and initializer of an instance field of a class with a constructor, when fields are initialized in the
    // constructor (not standard class fields): the value names the constructor declares, which the field can't use
    // (tsc's propertyWithInvalidInitializer), see scope_invalid_initializer_field()
    struct ClassItem const* instance_field;
    Span const* constructor_locals;
    u32 constructor_local_count;
    // A module or namespace block's exported declarations that conflict with an export of another block (TS2300): a
    // symbol of their own that isn't in the exports, which a value name in this block finds (tsc's local symbol of
    // declareModuleMember, an ExportValue whose exportSymbol is the duplicate)
    struct DuplicateExport* duplicate_exports;
    // A script's declarations that conflict with another file's global (TS2451, TS2300): a symbol of their own, which
    // tsgo's binder still has in the file's locals (the checker merges them into the globals later), so an expando
    // assignment `A.d = v` in the file declares into it
    struct DuplicateExport* script_duplicates;
} ScopeRare;

typedef struct DuplicateExport {
    struct Symbol* symbol;
    struct DuplicateExport* next;
} DuplicateExport;

// PERF: ~100k of them on nest/webpack, most of them a block that declares nothing: keep it small (what few scopes
// have goes to ScopeRare)
typedef struct Scope {
    Scope* parent;
    Binder* binder; // (Its errs are the scope's)
    // Null until something is declared in it (most scopes declare nothing): read it as it is (symbol_table_get() takes
    // null), write it through scope_locals_mut()
    SymbolTable* locals;
    Symbol* container; // Exported declarations go to its exports, and lookups see them
    // The resolve walk: the builder of the control flow container this scope is in (inherited by child scopes). Only
    // valid during the walk
    FlowBuilder* flow;
    FunctionImpl* function; // The function this scope is in, null outside of functions
    // The file the scope is in (null for the global scope)
    SourceFile* file;
    // The type parameter lists this scope is inside of (inherited by child scopes), null at the top level
    OuterGenerics const* generics;
    // The inside of the declaration of this symbol (a function, class, interface, type alias, enum or namespace
    // declaration): a use of it from here isn't a reference (tsc's isSelfReferenceLocation)
    Symbol const* self;
    // The statements declared in this scope (block_add_names()), null if none were
    struct Block const* block;
    ScopeRare* rare; // Null: all zero
    uint8_t kind; // ScopeKind
    // A computed member name, a member decorator or a constructor's parameters: `super` can't be used, tsc's
    // isLegalUsageOfSuperExpression (see resolve_super())
    NoSuper no_super;
    // Ambient namespace (or declaration file) without export declarations: everything is exported
    bool export_context : 1;
    bool ambient : 1;
    // Everything of a class's field (decorators, computed name, initializer): not directly in a constructor it's
    // nested in (tsc's isThisContainerOrFunctionBlock has PropertyDeclaration)
    bool in_field : 1;
    // A class member's decorator: its `this` is the one outside the class (tsc's getThisContainer of a Decorator)
    bool member_decorator : 1;
    bool parameters : 1;
    // statement_hoist_names() is declaring a nested statement's `var`s and functions here (SymbolDecl.hoisted)
    bool hoisting : 1;
    // Unless the downlevel emit moves the parameters into the body (tsc's useOuterVariableScopeInParameter)
    bool parameters_see_body : 1;
    // The return type annotation of a function: of the function's scope it sees only the parameters, not what the body
    // declares (tsc's resolveNameHelper: a FunctionScopedVariable only if it is a parameter, no types)
    bool return_type : 1;
    // Strict mode code (tsc's inStrictMode): a module, alwaysStrict, a "use strict" prologue, a class body
    bool strict : 1;
    // In a class (its type parameters, heritage clauses and body): strict because of it (tsc's getContainingClass in
    // getStrictModeIdentifierMessage)
    bool class_strict : 1;
    bool with_body : 1; // The body of a `with` statement: bound, never checked (tsc's checker skips it)
    // A static field of a class with legacy decorators: no `this` in its initializer (TS2816)
    bool decorated_static_field : 1;
    // An unbraced branch of an `if` (which declares a function in it): no block scope container to tsc's binder
    bool binder_transparent : 1;
} Scope;

extern ScopeRare const scope_rare_none;
extern const SymbolTable symbol_table_empty;
static inline SymbolTable const* scope_locals(Scope const* me) {
    return me->locals ? me->locals : &symbol_table_empty;
}
// Allocates them if needed
SymbolTable* scope_locals_mut(Scope* me);

static inline ScopeRare const* scope_rare(Scope const* me) {
    return my rare ? my rare : &scope_rare_none;
}
ScopeRare* scope_rare_new(Scope* me);
static inline ScopeRare* scope_rare_mut(Scope* me) {
    return my rare ? my rare : scope_rare_new(me);
}

// Symbol pointer -> symbol pointer, open addressing
typedef struct SymbolMap {
    Symbol const** keys;
    Symbol** values;
    u32 count;
    u32 cap;
} SymbolMap;

typedef struct Binder {
    Arena arena;
    // Scopes and their ScopeRare, apart from the rest: scope_release() pops walk-only scopes off it, which the flow nodes
    // and symbols made meanwhile don't get in the way of
    Arena scope_arena;
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
    // Some pattern has attributes (`declare module "*.css" with { type: "css" }`): not in ambient_modules, a symbol
    // of their own per pattern and attributes (tsgo's mergePatternAmbientModules), see binder_find_module()
    bool attributed_patterns;
    // Augmentations of a name only a pattern matches (`declare module "a.foo"` in a module against `"*.foo"`), by
    // name: a module of its own whose exports' parent is the pattern's (tsc's patternAmbientModuleAugmentations, the
    // unidirectional merge), which binder_find_module() gives that name instead of the pattern
    SymbolTable pattern_augmentations;
    Symbol** symbols; // malloc'd, all of them: aliases get resolved eagerly
    u32 symbol_count;
    u32 symbol_cap;
    // `declare module "m" {}` blocks in modules, in declaration order: each declares into a symbol of its own, which
    // binder_link() merges into the module it names (tsc's mergeModuleAugmentation)
    NamespaceDecl** augmentations; // malloc'd
    u32 augmentation_count;
    u32 augmentation_cap;
    // `declare global {}` blocks in modules, in declaration order: their declarations go into the globals after every
    // file's (tsc's initializeTypeChecker merges global augmentations after the files' locals), see
    // binder_declare_global_augmentations()
    NamespaceDecl** global_augmentations; // malloc'd
    u32 global_augmentation_count;
    u32 global_augmentation_cap;
    // Locals scope_check_export_merges() leaves to binder_link(): their alias declarations' spaces need the target
    struct PendingExportMerge* pending_export_merges; // malloc'd
    u32 pending_export_merge_count;
    u32 pending_export_merge_cap;
    // Declarations of one file that would merge into an alias another file declared in the same table (tsc's
    // mergeSymbol at checker initialization resolves the alias first): symbols of their own until binder_link()
    struct PendingAliasMerge* pending_alias_merges; // malloc'd
    u32 pending_alias_merge_count;
    u32 pending_alias_merge_cap;
    bool alias_merges_done; // binder_link() decided them: later declarations merge as usual
    // What `require` is in `require("m")` in a JavaScript file where nothing else is called that (tsc's requireSymbol:
    // any). Made when first needed
    Symbol* require_symbol;
    // Set while the program is typechecked: nothing makes symbols anymore (several checkers share them)
    bool frozen;
    // Values not found in class members, reported once types are known (scope_report_missing_prefixes())
    struct PendingNotFound* pending_not_found; // malloc'd
    u32 pending_not_found_count;
    u32 pending_not_found_cap;
    // Types used as values that may have meant a mapped type (TS2690 rather than TS2693), told by their types when
    // checking starts (see scope_report_type_as_value())
    struct PendingTypeAsValue* pending_type_as_value; // malloc'd
    u32 pending_type_as_value_count;
    // Where the errors of what is resolved but never checked go (binder_discarded_errs()), owned
    ErrorCtx* discarded_errs;
    // Some SymbolDecl.flow_container is set (a dot assignment declaration): only then can a read be one in the container
    // that declares it (TS2565, is_read_in_declaring_container())
    bool has_assignment_flow_containers;
    // The arenas of resolve threads other than the main one (plan 45), owned: two per thread, binder_thread_arena()'s
    // and its scope arena
    Arena* thread_arenas; // malloc'd
    u32 thread_arena_count;
} Binder;

// A TypeScript file's resolve walk on a thread of its own (plan 45, program_resolve_names()): what the walk would write
// to the binder goes here while `resolve_shard` is set, and the events whose outcome depends on the files walked before
// it (tsc's first ten spelling suggestions, expandos of symbols other files share) are deferred.
// resolve_shard_merge() replays and merges it, file by file in program order
typedef struct ResolveShard {
    Binder* binder;
    Arena* arena; // The thread's
    Arena* scope_arena; // The thread's (Binder.scope_arena)
    ErrorCtx errs; // The file's reports, parent: the program's (frozen meanwhile)
    ErrorCtx discarded; // binder_discarded_errs()
    Symbol** symbols; // malloc'd, what binder_new_symbol() made
    u32 symbol_count;
    u32 symbol_cap;
    struct PendingNotFound* pending_not_found; // malloc'd
    u32 pending_not_found_count;
    u32 pending_not_found_cap;
    struct PendingTypeAsValue* pending_type_as_value; // malloc'd
    u32 pending_type_as_value_count;
    struct PendingExportMerge* pending_export_merges; // malloc'd
    u32 pending_export_merge_count;
    u32 pending_export_merge_cap;
    struct DeferredEvent* deferred; // malloc'd
    u32 deferred_count;
    u32 deferred_cap;
} ResolveShard;
extern thread_local ResolveShard* resolve_shard;
// Where the binder's symbols, scopes and tables are allocated: the resolve thread's arena while one walks a file
static inline Arena* binder_arena(Binder* me) {
    return resolve_shard && resolve_shard->binder == me ? resolve_shard->arena : &my arena;
}
static inline Arena* binder_scope_arena(Binder* me) {
    return resolve_shard && resolve_shard->binder == me ? resolve_shard->scope_arena : &my scope_arena;
}
// Where name resolution reports: the walked file's own context on a resolve thread
static inline ErrorCtx* binder_errs(Binder* me) {
    return resolve_shard && resolve_shard->binder == me ? &resolve_shard->errs : my errs;
}
// Before the resolve walks: marks the files whose walk needs another file's walk done first (SourceFile.resolve_serial):
// a later class or interface declaration with type parameters shares the first declaration's type parameter symbols,
// which that one's walk makes (scope_new_generic_merged())
void binder_mark_serial_resolves(Binder* me);
// Whether the symbol's first declaration is in another file than `file`
bool symbol_first_declared_elsewhere(Symbol const* symbol, SourceFile const* file);
// An arena for resolve thread `t` (> 0; 0 is the main thread's, Binder.arena), kept until the binder dies
Arena* binder_thread_arena(Binder* me, u32 t);
Arena* binder_thread_scope_arena(Binder* me, u32 t);
// The shard's deferred events replayed (on the calling thread, no shard set), then its symbols, pending reports and
// errors merged into the binder and the program's errors; frees the shard's lists. Call in program order
void resolve_shard_merge(ResolveShard* me);

Binder* binder_new(ErrorCtx* errs);
// An ErrorCtx that lives as long as the binder and whose errors nobody reports: names resolved only for the
// baselines (a static block's decorators)
ErrorCtx* binder_discarded_errs(Binder* me);
void binder_destroy(Binder* me);
// Makes the globals of `parent` (which must outlive me and not change anymore) visible, without copying them
void binder_set_parent(Binder* me, Binder const* parent);
// Merges module augmentations, resolves every alias, so that nothing writes to a symbol afterwards, and reports
// import conflicts
void binder_link(Binder* me);
// The aliases the resolve walk declared after binder_link() (`import I = M` in a block): resolved at the end of name
// resolution, so the checkers (which share them) never write to them
void binder_resolve_remaining_aliases(Binder* me);
// A `declare module "m" {}` in a module, whose declarations binder_link() merges into "m"
void binder_add_augmentation(Binder* me, NamespaceDecl* decl);
// A `declare global {}` in a module, declared by binder_declare_global_augmentations() once every file is
void binder_defer_global_augmentation(Binder* me, NamespaceDecl* decl);
void binder_declare_global_augmentations(Binder* me);
Symbol* binder_new_symbol(Binder* me, csview name, u32 flags);
Symbol* binder_new_module_symbol(Binder* me, SourceFile* file);
// `globalThis`: a namespace whose exports are the globals
void binder_declare_global_this(Binder* me);
// The ambient module of that name (no quotes), made if it doesn't exist yet: every `declare module "m"` block and
// every augmentation of it (binder_link()) declare into the same symbol
Symbol* binder_ambient_module(Binder* me, csview name, Span name_span, struct TypeNode const* attributes);
// A `declare module "m" {}` block of a script is a declaration of the ambient module
void binder_add_module_declaration(Symbol* module, NamespaceDecl const* decl);
// The module a string names: an ambient module, the file the program found for it if that is a module, a pattern
// ambient module. Null if there is none (module_ref_is_missing() tells "not found" from "not a module")
Symbol* binder_module_of_ref(Binder const* me, ModuleRef* ref);
// An import's attributes as pattern ambient modules are matched against them: name -> string value
typedef struct ModuleAttr {
    csview name;
    csview value;
    bool known; // The value is a string literal (else only a pattern's `string`, or what it can't tell, takes it)
} ModuleAttr;
typedef struct ModuleAttrs {
    ModuleAttr items[16];
    u32 count;
    // From a type the checker knows (`import("m", options)` of a variable): binder_module_of_ref() can't tell
    bool needs_types;
} ModuleAttrs;
// The attributes ref has (from its import, export, import type, or `import()`'s literal options)
void module_ref_syntactic_attributes(ModuleRef const* ref, ModuleAttrs* ret);
struct ImportAttributes;
struct CallExpr;
// Keeps a copy of the attributes of an import (`attributes`) or of `import(m, options)` (`call`) in ref (parser)
void module_ref_set_attributes(ModuleRef* ref, struct ImportAttributes const* attributes, struct CallExpr const* call);
typedef struct ModuleRefAttrs {
    ModuleAttrs attrs; // Views into storage
    char* storage;
} ModuleRefAttrs;
static inline void module_ref_attrs_free(ModuleRefAttrs* me) {
    if (!me) return;
    free(my storage);
    free(me);
}
// What ref is with these attributes (the checker's, for `import("m", options)`)
Symbol* binder_find_module_with(Binder const* me, ModuleRef const* ref, ModuleAttrs const* attributes);
// Whether any pattern ambient module has attributes: only then do attributes change what an import is
bool binder_has_attributed_patterns(Binder const* me);
// What importing the module gives: the target of its `export =` if it has one (the `export =` itself when the
// module's types made it a namespace, bind_type_exports()), else itself
Symbol* module_export_equals(Symbol* module);
// How tsc names a module in a message: an ambient module by its declared name, a file by the specifier it was
// imported with (TS2305, ref may be null otherwise) or by its path without the extension. With the quotes
cstr module_name_for_error(Symbol const* module, ModuleRef const* ref, bool by_specifier);
cstr symbol_fully_qualified_name(Symbol const* symbol);

Scope* scope_new(Scope* parent, ScopeKind kind);
// A walk-only scope (a parameter list's, a block's) whose walk is over: freed if nothing can point at it, which is when
// it is still the scope arena's latest allocation (no child scope outlived its walk; its ScopeRare may come right after
// it), it declares nothing, no symbol or declaration was made while it was walked (SymbolDecl.block,
// SymbolAlias.scope) and no pending report took it. Whatever else stores a Scope pointer (malloc'd lists, the AST)
// must keep the scope from being released like the pending reports do, or clear its pointer when it was released.
// `pending_mark` is scope_pending_mark() from when it was made
typedef struct ScopePendingMark { u32 not_found, export_merges, deferred; u64 made; } ScopePendingMark;
ScopePendingMark scope_pending_mark(Binder const* binder);
bool scope_release(Scope* me, ScopePendingMark pending_mark); // Whether it was released
// The innermost for-in loop whose body `names` is in and whose variable is `variable` (null if none)
struct ForEachLoopStatement const* scope_for_in_loop_of(Scope const* names, Symbol const* variable);
// What tsc's getContainingFunction finds for a node in this scope: Scope.function, or the function whose parameter a
// decorator is of
struct FunctionImpl const* scope_containing_function(Scope const* me);
// A scope made ahead of the resolve walk (ClassDecl.name_scope) takes what scope_new() inherits from where the walk
// reaches it
void scope_reparent(Scope* me, Scope* parent);
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
/// The name of a class declaration declared in me: strict code of the class
void scope_check_strict_class_name(Scope const* me, Span name);
// tsc's checkStrictModeEvalOrArguments: `eval` and `arguments` can't be declared or assigned in strict mode code
void scope_check_strict_eval_or_arguments(Scope const* me, Span name);
// tsc's checkStrictModeFunctionDeclaration: below ES2015, no function declarations in the blocks of strict mode code
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

// The program's version of a symbol: the overlay if it added to a symbol of the shared lib program
Symbol const* binder_redirect_slow(Binder const* me, Symbol const* symbol);
static inline Symbol const* binder_redirect(Binder const* me, Symbol const* symbol) {
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
// False if it isn't at the top level of a file or namespace (reported)
bool scope_export_default_expr(Scope* me, Expr* expr, struct TypeNode const* jsdoc_type, Span statement, bool export_equals);
// The exports of a CommonJS file: every `exports.x = v`, `module.exports.x = v`, `Object.defineProperty(exports, ...)`
// declares x and `module.exports = v` the `export=`, wherever it is (tsc's getAssignmentDeclarationKind), then its
// `module` and `exports` variables
void scope_add_commonjs_module(Scope* me);
// The expandos of the statements of a block (declared in `me`): `f.x = v` where f is a function declared in the block
// or its function (or file), or a variable initialized with a function (JavaScript: also a class or `{}`), declares x
// in f's exports (tsc's bindSpecialPropertyAssignment)
void scope_add_expandos(Scope* me, struct Block const* block);
// An assignment `f.x = v` nested in an expression (`(f.x = 1)`, `if (f.x = 1)`), which tsc's binder binds like any
// other: called by the resolve walk for the ones scope_add_expandos() didn't see
void scope_add_nested_expando(Scope* me, struct BinaryOpExpr* op);
// Whether a declaration initialized (or a member assigned) with `init` is an expando container (tsgo's
// IsExpandoInitializer): a function or arrow function, in JavaScript also a class expression or `{}` (unless
// `annotated`): that expression, null otherwise
Expr* expando_initializer(Expr* init, bool js, bool annotated);
// Whether an expando host is a variable (or assignment declaration) initialized with a function or class expression,
// whose symbol is the host in tsgo (getInitializerSymbol): a symbol without a parent, named by getNameOfSymbolAsWritten.
// `name`: the expression's own name, else invalid (the host's name)
bool expando_host_is_initializer(Symbol const* host, Span* name);
// TypeScript `this.x = v` in a constructor or static block (`left` is `this.x`, also a target of a destructuring
// assignment): marks the field it types (ClassItem.field.ts_assigned)
void scope_note_ts_this_assignment(Scope const* me, struct Expr const* left);
// JavaScript `this.x = v` (`op`, whose left side was just resolved): declares x as an instance member of the class
// `this` is of (a static member in a static method), or of the object literal whose method it is in (tsgo's
// bindThisPropertyAssignment)
void scope_add_this_property(Scope* me, struct BinaryOpExpr* op);
// `require` in a `require("m")` of a JavaScript file that doesn't declare the name
Symbol* binder_require_symbol(Binder* me);

// Whether the scope's own tables (its locals, its container's exports) have the symbol
bool scope_declares_symbol(Scope const* me, Symbol const* symbol);
// A type parameter of a class that `me` is in the base class expression of (ScopeRare.base_expression_of)
bool scope_hides_base_expression_type_parameter(Scope const* me, Symbol const* symbol);
// Lookup by meaning, seeing through aliases. The symbol found may be an alias, see symbol_declaration()
// tsc's getSpellingSuggestion: the candidate name closest to `name`, if any is close enough. `names` are `count`
// strings, returns the index of the best, or -1
isize spelling_suggestion(csview name, csview const* names, isize count);
// With tsgo's tie-break: of the candidates at the best distance, the least by `compare(ctx, a, b)`
isize spelling_suggestion_ex(csview name, csview const* names, isize count, int (*compare)(void* ctx, isize a, isize b), void* ctx);
// A `compare` for spelling_suggestion_ex(): `ctx` is the csview array, by text (tsgo's strings.Compare)
int spelling_compare_strings(void* ctx, isize a, isize b);
// tsc's getSuggestedSymbolForNonexistentModule: an export of container with a name close to name's, reported as
// TS2724 (tsc's errorNoModuleMemberSymbol and resolveEntityName). False if there is none
bool scope_report_misspelled_export(Symbol const* container, Span name, csview container_name, bool declared_here, ErrorCtx* errs);
// tsc's reportNonExportedMember: `import { x }` of a module without an export x (TS2305), which declares x locally
// (TS2459/TS2460), or whose `export =` is x (TS2595-TS2598, TS2616/TS2617)
void scope_report_non_exported_member(PortItem const* item, Symbol const* module, ErrorCtx* errs);
void scope_report_default_import_of_export_equals(PortItem const* item, Symbol const* module, ErrorCtx* errs);
// TS6203 "was also declared here" (TS6204 "and here" from the second on, at most 5) on the duplicate error at `at`,
// pointing at `other` (tsc's addDuplicateDeclarationError)
void add_duplicate_related(Error* error, Span at, Span other);
void add_duplicate_related_named(Error* error, Span at, Span other, csview name); // Naming the symbol so
// A name used with `meaning` wasn't found: TS2304, or TS2552 with the closest name in scope (tsc's
// onFailedToResolveSymbol with getSuggestedSymbolForNonexistentSymbol). `shorthand`: the value of `{ a }` (TS18004)
void scope_report_not_found(Scope const* me, Span name, u32 meaning, bool shorthand, ErrorCtx* errs);
// A JSX factory `name` (what `at` needs) isn't in scope: TS2552 with a spelling suggestion, else TS2874
void scope_report_jsx_factory_not_found(Scope const* me, Span at, csview name, u32 meaning, ErrorCtx* errs);
// TS2693 for a type used as a value, TS2690 if it may have meant a mapped type `{ [P in K]: ... }` (`maybe_mapped`: the
// name is the lone computed key of a type literal, tsc's maybeMappedType, which is told by the type when checking starts)
void scope_report_type_as_value(Binder* binder, Symbol const* type, Span name, bool maybe_mapped, ErrorCtx* errs);
// TS2503, or TS2833 for a namespace with a close name in scope (tsc's resolveEntityName with a namespace meaning)
void scope_report_namespace_not_found(Scope const* me, Span at, ErrorCtx* errs);
// A name not found that tsc counts among its suggestions without making one (`await` outside an async function)
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
// A `this` expression or type at `me`: sets FunctionImpl.contains_this of the function it is in (through arrow functions
// and function types)
void scope_note_this_reference(Scope const* me);
// The innermost class body this scope is in (through functions), null if none
struct ClassDecl const* scope_enclosing_class(Scope const* me);
