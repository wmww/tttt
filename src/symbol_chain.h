#pragma once

#include "scope.h"

// Symbol accessibility: how tsc names a symbol from a location (checker.ts lookupSymbolChain / getSymbolChain /
// getAccessibleSymbolChain / needsQualification / getContainersOfSymbol, and symbolToExpression's printing). Used by
// the symbols baseline, which prints symbolToString(symbol, node.parent): no meaning, so nothing ever needs
// qualification because of a name that shadows it, and a symbol is named by the first table in scope that has it
// under its own name or through an alias.
//
// The location is a Scope. What stands for tsc's symbol tables in scope, from the inside: the locals of every
// block/function scope, a module's or namespace's locals and then its exports, the globals.

#define SYMBOL_CHAIN_MAX 16

typedef struct SymbolChain {
    Symbol const* items[SYMBOL_CHAIN_MAX]; // Outermost first, the symbol (or what names it) last
    u32 len;
} SymbolChain;

// tsc's getSymbolChain(symbol, meaning, endOfChain = true). meaning 0 is tsc's `undefined`. yield_module: a module
// that nothing names can start the chain (the types printer then writes `import("m")`), else it's left out
SymbolChain symbol_chain_at(Scope const* enclosing, Symbol const* symbol, u32 meaning, bool yield_module);
// `import * as ns`'s own symbol (make_gather_target()) when tsc resolves it to a clone of the module
// (resolveESModuleSymbol under esModuleInterop): it names its own type, never the module's
bool namespace_import_is_clone(Symbol const* symbol);
// `import * as ns` of a module with `export =` that tsgo resolves to a clone (the assigned value has signatures or a
// `default`)
bool export_equals_namespace_import_is_clone(Symbol const* alias);
// The chain of a container of members (a class, an interface, an enum...) that qualifies them: tsc's
// getSymbolChain(container, Namespace, endOfChain = false) from getContainersOfSymbol of a member. Empty when the
// member is named bare (the container is a module or an anonymous type)
SymbolChain symbol_member_container_chain_at(Scope const* enclosing, Symbol const* container);
// Appends a chain the way symbolToExpression prints it: `a.b`, `a["b c"]`, `a[1]`
void symbol_chain_append(cstr* out, Scope const* enclosing, SymbolChain const* chain);
// The same for an entity name (types printer): a quoted member name that is an identifier is written without quotes;
// `after_module`: the chain continues an `import("m")`, its first name isn't the initial one of the entity name (an
// `export default` declaration is `default` there, tsc's InInitialEntityName)
void symbol_chain_append_entity(cstr* out, Scope const* enclosing, SymbolChain const* chain, bool entity, bool after_module);
// Appends `name` as the next part of an expression printed by symbol_chain_append (`.name`, `["name"]`...)
void symbol_chain_append_member(cstr* out, csview name, bool is_enum_member);

// Symbol accessibility for declaration emit (checker.ts isSymbolAccessible and hasVisibleDeclarations): whether a
// `.d.ts` written for the enclosing scope's file could name the symbol. With tsc's shouldComputeAliasesToMakeVisible:
// a declaration that isn't exported but is directly in a visible statement list counts as visible (declaration emit
// would "late paint" it, emitting it too)
typedef enum SymbolAccessibility : uint8_t {
    SYMBOL_ACCESSIBLE,
    SYMBOL_NOT_ACCESSIBLE,
    SYMBOL_CANNOT_BE_NAMED, // Declared in another external module, and nothing here names it
} SymbolAccessibility;

typedef struct SymbolAccess {
    SymbolAccessibility kind;
    // tsc's errorModuleName: NOT_ACCESSIBLE, the container whose chain was accessible when that isn't the symbol
    // (named from the enclosing scope); CANNOT_BE_NAMED, the symbol's external module (named fully qualified). Null
    // when there is none
    Symbol const* module;
    // ACCESSIBLE through a chain: the symbol it starts with, whose declarations may need to be emitted too (tsc's
    // aliasesToMakeVisible are among them). Null otherwise
    Symbol const* head;
} SymbolAccess;

// tsc's isSymbolAccessible(symbol, enclosing, meaning, shouldComputeAliasesToMakeVisible = true)
SymbolAccess symbol_accessibility_at(Scope const* enclosing, Symbol const* symbol, u32 meaning);
// tsc's isValueSymbolAccessible: accessible as a value (shouldComputeAliasesToMakeVisible false)
bool symbol_value_accessible_at(Scope const* enclosing, Symbol const* symbol);
// tsc's isTypeSymbolAccessible, the same as a type
bool symbol_type_accessible_at(Scope const* enclosing, Symbol const* symbol);
// tsc's IsSymbolAccessibleByFlags: accessible with `meaning` without going through a module (allowModules false)
bool symbol_accessible_without_modules_at(Scope const* enclosing, Symbol const* symbol, u32 meaning);
// tsc's hasVisibleDeclarations with late painting: every declaration is visible or directly in a visible statement
// list (a file, a visible namespace's block)
bool symbol_has_visible_declarations(Symbol const* symbol);
// Declaration emit's late painted declarations on this thread (null: none): a painted namespace is visible like an
// exported one (tsc's addVisibleAlias sets its isVisible). `symbols` and `count` are read where they are each time
void symbol_chain_set_painted(Symbol const* const* const* symbols, u32 const* count);
// What declaration walks painted earlier (null: nothing), visible like what the current walk painted: tsc's isVisible
// stays set once emit painted it, which the suite's types and symbols walk after emit sees
void symbol_chain_set_kept_painted(Symbol const* const* symbols, u32 count);
// How a module's file is named from where symbols are being named on this thread (tsc's getSpecifierForModuleSymbol,
// with the declaration emitter's resolver host), which decides between the containers a symbol can be named through
// (getSymbolChain's sortByBestName). Null (the default): by the file's path, as tsc's printer without a host does
typedef cstr (*ModuleSpecifierFn)(void* ctx, struct SourceFile const* file);
// The import or export declaration (its ModuleRef) whose name the chains asked for next on this thread are printed for,
// null for none (tsgo's enclosing declaration inside one: its usage mode finds the file's imports again)
void symbol_chain_set_usage(struct ModuleRef const* usage_ref, bool for_specifier);
// The function whose expando members declaration emit is writing as a namespace's (`declare namespace f { var x: T }`),
// null for none: their names shadow the file's in the chains asked for meanwhile
void symbol_chain_set_expando_host(Symbol const* host);
void symbol_chain_set_module_specifier(ModuleSpecifierFn fn, void* ctx);
// Symbols are named from inside a named class expression's body on this thread until the matching leave: a CommonJS
// export's class is named by its own name there, not through the module (tsc's enclosing declaration)
void symbol_chain_enter_class(struct ClassDecl const* class_decl);
void symbol_chain_leave_class(void);
// Frees this thread's accessibility memo of the declaration walks (a thread that checked is done)
void symbol_chain_release_thread(void);
// tsc's isDeclarationVisible of the statements directly in a scope (a file is, a namespace block is if its namespace
// declaration is, nothing else is)
bool scope_statements_visible(Scope const* scope);
// The scope of the external module (a module file, `declare module "m" {}`) a scope is in, tsc's
// getExternalModuleContainer. Null in a script
Scope const* scope_external_module(Scope const* scope);
