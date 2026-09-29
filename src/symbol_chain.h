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
// The chain of a container of members (a class, an interface, an enum...) that qualifies them: tsc's
// getSymbolChain(container, Namespace, endOfChain = false) from getContainersOfSymbol of a member. Empty when the
// member is named bare (the container is a module or an anonymous type)
SymbolChain symbol_member_container_chain_at(Scope const* enclosing, Symbol const* container);
// Appends a chain the way symbolToExpression prints it: `a.b`, `a["b c"]`, `a[1]`
void symbol_chain_append(cstr* out, Scope const* enclosing, SymbolChain const* chain);
// The same for an entity name (types printer): a quoted member name that is an identifier is written without quotes
void symbol_chain_append_entity(cstr* out, Scope const* enclosing, SymbolChain const* chain, bool entity);
// Appends `name` as the next part of an expression printed by symbol_chain_append (`.name`, `["name"]`...)
void symbol_chain_append_member(cstr* out, csview name, bool is_enum_member);
