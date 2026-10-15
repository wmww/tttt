#include "symbol_chain.h"
#include "class_decl.h"
#include "unicode_identifiers.h"
#include "compiler_options.h"
#include "port_statement.h"
#include "source_file.h"
#include "ty/ty.h"
#include "ty/ty_internal.h"
#include "statement.h"
#include "program.h"

// The symbol tables in scope at a location, from the inside (tsc's forEachSymbolTableInScope)

#define SCOPE_TABLES_MAX 64

typedef struct ScopeTable {
    SymbolTable const* table;
    bool local_lookup; // tsc's isLocalNameLookup
    // A module's or namespace's locals: tsc's also have the exported declarations of the block (a local whose
    // exportSymbol is the export), in declaration order. These are the exports, the ones declared in `file` count
    SymbolTable const* exports;
    Symbol const* container;
    SourceFile const* file;
} ScopeTable;

typedef struct VisitedTable {
    Symbol const* symbol;
    SymbolTable const* table;
} VisitedTable;

typedef struct ChainCtx {
    Scope const* enclosing;
    Binder const* binder;
    ScopeTable tables[SCOPE_TABLES_MAX];
    u32 table_count;
    bool in_external_module;
    bool only_external_aliasing;
    bool yield_module;
    bool in_global_this; // Looking through globalThis's exports: they don't fall back to globalThis again
    // Looking for a chain to a container of the symbol being named (getSymbolChain of a parent), see
    // namespace_import_is_clone()
    bool container_lookup;
    // tsc's visitedSymbolTablesMap: the tables being searched for each symbol, so that aliases of each other end
    VisitedTable visited[64];
    u32 visited_count;
} ChainCtx;

// During a declaration walk (symbol_chain_set_painted()): the aliases of the big tables (the globals), which
// trySymbolTable would otherwise find by looking at every entry, for every name the printer writes. The tables don't
// change while checking; the index starts over with every walk
#define ALIAS_INDEX_SLOTS 64
#define ALIAS_INDEX_MAX 24
#define ALIAS_INDEX_MIN_TABLE 64

typedef struct AliasIndex {
    SymbolTable const* table;
    u32 generation;
    u32 alias_count; // ALIAS_INDEX_MAX + 1: too many, scan the table
    u32 aliases[ALIAS_INDEX_MAX];
} AliasIndex;

static thread_local AliasIndex alias_indexes[ALIAS_INDEX_SLOTS];
static thread_local u32 alias_index_generation; // 0: not in a declaration walk

static AliasIndex const* alias_index_of(SymbolTable const* table) {
    if (!alias_index_generation || table->count < ALIAS_INDEX_MIN_TABLE) return nullptr;
    AliasIndex* slot = &alias_indexes[((uintptr_t)table >> 4) % ALIAS_INDEX_SLOTS];
    if (slot->table != table || slot->generation != alias_index_generation) {
        *slot = (AliasIndex){ .table = table, .generation = alias_index_generation };
        for (u32 i = 0; i < table->count; i++) {
            if (!(table->entries[i].symbol->flags & SYMBOL_ALIAS)) continue;
            if (slot->alias_count == ALIAS_INDEX_MAX) {
                slot->alias_count++;
                break;
            }
            slot->aliases[slot->alias_count++] = i;
        }
    }
    return slot->alias_count > ALIAS_INDEX_MAX ? nullptr : slot;
}

static void add_table(ChainCtx* me, SymbolTable const* table, bool local_lookup) {
    if (!table || my table_count == SCOPE_TABLES_MAX) return;
    for (u32 i = 0; i < my table_count; i++) {
        if (my tables[i].table == table) return;
    }
    my tables[my table_count++] = (ScopeTable){ .table = table, .local_lookup = local_lookup };
}

static void add_locals_with_exports(ChainCtx* me, SymbolTable const* locals, Symbol const* container, SourceFile const* file) {
    if (my table_count == SCOPE_TABLES_MAX) return;
    SymbolTable const* exports = container ? symbol_members(container)->exports : nullptr;
    my tables[my table_count++] = (ScopeTable){ .table = locals, .local_lookup = true, .exports = exports, .container = container, .file = file };
    add_table(me, exports, true);
}

static void chain_ctx_init(ChainCtx* me, Scope const* enclosing) {
    *me = (ChainCtx){ .enclosing = enclosing, .binder = enclosing->binder };
    for (Scope const* scope = enclosing; scope; scope = scope->parent) {
        switch (scope->kind) {
            case SCOPE_BLOCK:
            case SCOPE_FUNCTION:
            case SCOPE_STATIC_BLOCK:
                add_table(me, scope_locals(scope), true);
                break;
            case SCOPE_MODULE:
            case SCOPE_NAMESPACE:
                add_locals_with_exports(me, scope_locals(scope), scope->container, scope->file);
                break;
            case SCOPE_SCRIPT: // Locals of a script are globals
            case SCOPE_GLOBAL_AUGMENTATION:
            case SCOPE_ENUM: // (An enum declaration has no locals: its members aren't in scope for tsc's walk)
                break;
            case SCOPE_GLOBAL:
                break;
        }
        // (A CommonJS file has no externalModuleIndicator, only a commonJsModuleIndicator: tsc's isExternalModule is
        // false there, so a UMD global is accessible)
        if (scope->kind == SCOPE_MODULE && !(scope->file && scope->file->is_commonjs)) my in_external_module = true;
    }
    add_table(me, &my binder->globals, true);
}

static Symbol const* merged(ChainCtx const* me, Symbol const* symbol) {
    return binder_redirect(my binder, symbol);
}

// tsc's resolveSymbol(): an alias that is nothing else is seen through
static Symbol const* resolve_symbol(Symbol const* symbol) {
    if (symbol && (symbol->flags & (SYMBOL_ALIAS | SYMBOL_ANY_MEANING)) == SYMBOL_ALIAS && !symbol->is_placeholder) {
        return symbol_resolve_alias((Symbol*)symbol);
    }
    return symbol;
}

// tsc's resolveAlias(): what an alias names
static Symbol const* resolve_alias(Symbol const* symbol) {
    if (!(symbol->flags & SYMBOL_ALIAS)) return symbol;
    Symbol const* ret = symbol_resolve_alias((Symbol*)symbol);
    // (`export = C.B`, a static B and an interface B, which are one symbol in tsc: it stands for the interface here)
    if (ret && ret->flags & SYMBOL_ALIAS && ret->alias->value_target) ret = symbol_resolve_alias((Symbol*)ret);
    return ret;
}

// `import * as ns` that tsc resolves to a clone of the module (resolveESModuleSymbol under esModuleInterop when the
// module's type has a `default` or signatures: cloneTypeAsModuleType with a synthetic default), which doesn't name the
// module. tttt's type of `ns` is the module's, so this only matters where the module is a container something is
// named through (ChainCtx.container_lookup); the module's own type is printed through `ns` like tsc prints the
// clone's. HACK
bool namespace_import_is_clone(Symbol const* symbol) {
    if (!symbol_rare(symbol)->gather_module || !symbol->decl_count) return false;
    SymbolDecl const* decl = &symbol->decls[0];
    PortItem const* item = decl->kind == SYMBOL_DECL_ALIAS ? decl->node : nullptr;
    // (Not `import x = require()` nor a JavaScript `require`: tsc's resolveExternalModuleSymbol doesn't clone)
    if (!item || item->tag != PORT_IMPORT_GATHER || item->is_require) return false;
    Symbol* module = symbol_rare(symbol)->gather_module;
    if (symbol_get_export(module, c_sv("default"))) return true;
    Symbol const* assigned = module->is_module ? module_export_equals(module) : module;
    if (assigned && assigned->flags & SYMBOL_ALIAS) assigned = resolve_alias(assigned);
    return assigned && (assigned->flags & (SYMBOL_FUNCTION | SYMBOL_CLASS));
}

// `import * as ns` of a module with `export =` that tsc resolves to a clone (resolveESModuleSymbol under esModuleInterop
// when what it assigns has signatures or a `default`): ns is a symbol of its own then, which names neither the module nor
// what it assigns (its type prints by ty_display.c's display_module_clone())
static bool has_port_tag(Symbol const* symbol, u32 tag);
static bool export_equals_namespace_import_is_clone(Symbol const* alias) {
    if (!has_port_tag(alias, PORT_IMPORT_GATHER) || alias->alias->item->is_require || !alias->alias->item->module_ref) return false;
    Symbol* module = binder_module_of_ref(alias->binder, alias->alias->item->module_ref);
    Symbol const* assigned = module ? module_export_equals(module) : nullptr;
    if (!assigned || assigned == module || assigned->is_placeholder) return false;
    if (assigned->flags & SYMBOL_ALIAS) assigned = resolve_alias(assigned);
    if (assigned && !(assigned->flags & (SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE | SYMBOL_VARIABLE))) return false;
    return assigned && ((assigned->flags & (SYMBOL_FUNCTION | SYMBOL_CLASS)) || symbol_get_export((Symbol*)assigned, c_sv("default")));
}

// The identity of what a symbol stands for: a namespace import (`import * as ns`) is its module
static Symbol const* identity(ChainCtx const* me, Symbol const* symbol) {
    return merged(me, symbol_members(merged(me, symbol)));
}

// tsc's getSymbolIfSameReference()
static bool same_reference(ChainCtx const* me, Symbol const* a, Symbol const* b) {
    return a && b && identity(me, resolve_symbol(a)) == identity(me, resolve_symbol(b));
}

// tsc's hasNonGlobalAugmentationExternalModuleSymbol for any declaration: a module file or `declare module "m"`
static bool is_external_module_symbol(Symbol const* symbol) {
    return symbol->is_module || symbol_is_augmented_export_equals(symbol);
}

static bool is_export_specifier(Symbol const* symbol) {
    if (!(symbol->flags & SYMBOL_ALIAS) || !symbol->alias || !symbol->alias->item) return false;
    u32 tag = symbol->alias->item->tag;
    return tag == PORT_EXPORT_SPECIFIC || tag == PORT_REEXPORT_SPECIFIC;
}

static bool has_port_tag(Symbol const* symbol, u32 tag) {
    return (symbol->flags & SYMBOL_ALIAS) && symbol->alias && symbol->alias->item && symbol->alias->item->tag == tag;
}

static u32 left_meaning(u32 meaning) {
    return meaning == SYMBOL_VALUE ? SYMBOL_VALUE : SYMBOL_NAMESPACE;
}

static bool chain_push(SymbolChain* chain, Symbol const* symbol) {
    if (chain->len == SYMBOL_CHAIN_MAX) return false;
    chain->items[chain->len++] = symbol;
    return true;
}

static bool chain_prepend(SymbolChain* chain, Symbol const* symbol) {
    if (chain->len == SYMBOL_CHAIN_MAX) return false;
    memmove(chain->items + 1, chain->items, chain->len * sizeof(*chain->items));
    chain->items[0] = symbol;
    chain->len++;
    return true;
}

static bool needs_qualification(ChainCtx* me, Symbol const* symbol, u32 meaning);
static csview export_name(Symbol const* symbol);
static bool accessible_chain(ChainCtx* me, Symbol const* symbol, u32 meaning, SymbolChain* out);

static bool accessible_chain_from_table(
    ChainCtx* me, Symbol const* symbol, u32 meaning, SymbolTable const* table, bool ignore_qualification,
    bool local_lookup, SymbolChain* out
);

// tsc's canQualifySymbol
static bool can_qualify(ChainCtx* me, Symbol const* from_table, u32 meaning) {
    if (!needs_qualification(me, from_table, meaning)) return true;
    SymbolChain parent;
    return from_table->parent && accessible_chain(me, from_table->parent, left_meaning(meaning), &parent);
}

// tsc's isAccessible
static bool is_accessible(
    ChainCtx* me, Symbol const* symbol, u32 meaning, Symbol const* from_table, Symbol const* resolved,
    bool ignore_qualification
) {
    if (!from_table) return false;
    Symbol const* named = resolved ? resolved : from_table;
    if (named != symbol && identity(me, named) != identity(me, symbol)) {
        // (tsgo's isAccessible resolves an alias that resolves to another alias on: an alias merged with a type, `type
        // D = ...; export * as D from "m"`, is an alias that resolveAlias doesn't see through)
        bool like = false;
        for (u32 depth = 0; resolved && !like && (resolved->flags & SYMBOL_ALIAS) && depth < 8; depth++) {
            Symbol const* next = symbol_resolve_alias((Symbol*)resolved);
            if (!next || next == resolved) break;
            resolved = next;
            like = resolved == symbol || identity(me, resolved) == identity(me, symbol);
        }
        if (!like) return false;
        named = resolved;
    }
    if (symbol->is_module && named != symbol && namespace_import_is_clone(merged(me, named))) return false;
    // (and nothing but itself names the clone)
    if (named != symbol && symbol_rare(symbol)->gather_module && namespace_import_is_clone(symbol)) return false;
    // (Nor does it name what the module assigns: the clone is a symbol of its own, printed by ty_display.c's
    // display_module_clone())
    if (from_table != symbol && export_equals_namespace_import_is_clone(from_table)) return false;
    // (A table holds the symbol as it was declared: an `export =` target augmented later is tsc's merged clone, so
    // only modules themselves are skipped here)
    if (from_table->is_module) return false;
    return ignore_qualification || can_qualify(me, merged(me, from_table), meaning);
}

// tsc's getCandidateListForSymbol
static bool candidate_list(
    ChainCtx* me, Symbol const* symbol, u32 meaning, Symbol const* from_table, Symbol const* resolved,
    bool ignore_qualification, SymbolChain* out
) {
    if (is_accessible(me, symbol, meaning, from_table, resolved, ignore_qualification)) {
        out->len = 0;
        chain_push(out, from_table);
        return true;
    }
    if (!resolved) return false;
    SymbolTable const* table = symbol_exports_table(resolved);
    if (!table) return false;
    SymbolChain sub;
    if (accessible_chain_from_table(me, symbol, meaning, table, true, false, &sub) &&
        can_qualify(me, from_table, left_meaning(meaning))
    ) {
        out->len = 0;
        chain_push(out, from_table);
        for (u32 i = 0; i < sub.len; i++) chain_push(out, sub.items[i]);
        return true;
    }
    return false;
}

static bool try_entry(
    ChainCtx* me, Symbol const* symbol, u32 meaning, csview name, Symbol const* entry, bool ignore_qualification,
    bool local_lookup, SymbolChain* out
) {
    if (!(entry->flags & SYMBOL_ALIAS)) return false;
    if (csview_equals(name, "export=") || csview_equals(name, "default")) return false;
    if (has_port_tag(entry, PORT_EXPORT_AS_NAMESPACE) && my in_external_module) return false;
    if (my only_external_aliasing && !(has_port_tag(entry, PORT_IMPORT_GATHER) && entry->alias->item->is_require)) {
        return false;
    }
    if (local_lookup && has_port_tag(entry, PORT_REEXPORT_GATHER)) return false;
    if (!ignore_qualification && is_export_specifier(entry)) return false;
    // (`import { T } from "m"` of a type T of the namespace merged with the value m's `export =` names, which may
    // have a property T too: tsc's getExternalModuleMember combines them into a symbol of their own
    // (combineValueAndTypeSymbols), which names neither)
    Symbol const* value_target = entry->alias ? entry->alias->value_target : nullptr;
    // (Only when the value has the property: the binder makes one for any name, whether it does is up to the checker)
    if (value_target && value_target->decl_count && value_target->decls[0].kind == SYMBOL_DECL_EXPORT_EQUALS_PROPERTY &&
        (!current_checker || !ty_export_equals_property_missing(current_checker, value_target))
    ) {
        return false;
    }
    Symbol const* resolved = resolve_alias(entry);
    if (!resolved) return false;
    return candidate_list(me, symbol, meaning, entry, resolved, ignore_qualification, out);
}

// tsc 7's trySymbolTable keeps every candidate chain and picks the first shortest one (compareSymbolChains: by
// length, then each symbol by compareSymbols), where 6.0 took the first in table order: `import b = a.b` names
// `a.b` as `b` wherever both are in scope
static int compare_chains(SymbolChain const* a, SymbolChain const* b) {
    if (a->len != b->len) return a->len < b->len ? -1 : 1;
    for (u32 i = 0; i < a->len; i++) {
        int c = ty_compare_symbols(a->items[i], b->items[i]);
        if (c) return c;
    }
    return 0;
}

static void keep_best_chain(SymbolChain* best, bool* found, SymbolChain const* candidate) {
    if (!*found || compare_chains(candidate, best) < 0) *best = *candidate;
    *found = true;
}

static char const* declared_at(Symbol const* symbol, SourceFile const* file) {
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (span_source(symbol->decls[i].name) == file && span_valid(symbol->decls[i].name)) return symbol->decls[i].name.buf;
    }
    return nullptr;
}

// Whether the symbol's declaration in the file is a function declaration: tsc's binder binds those of a block before
// its other statements (bindEachFunctionsFirst), so they come first in the block's locals
static bool declared_as_function(Symbol const* symbol, SourceFile const* file) {
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (span_source(symbol->decls[i].name) == file && span_valid(symbol->decls[i].name)) {
            return symbol->decls[i].kind == SYMBOL_DECL_FUNCTION;
        }
    }
    return false;
}

typedef struct LocalEntry {
    SymbolEntry const* entry;
    char const* at;
    bool exported;
    bool function;
} LocalEntry;

// The order of tsc's locals: insertion order, function declarations first
static int compare_local_entries(void const* a, void const* b) {
    LocalEntry const* l = a;
    LocalEntry const* r = b;
    if (l->function != r->function) return l->function ? -1 : 1;
    return l->at < r->at ? -1 : l->at > r->at;
}

// trySymbolTable over the locals of a module or namespace block, whose exported declarations are locals too for tsc
static bool try_locals_with_exports(
    ChainCtx* me, Symbol const* symbol, u32 meaning, ScopeTable const* scope_table, SymbolChain* out
) {
    SymbolTable const* locals = scope_table->table;
    SymbolTable const* exports = scope_table->exports;
    if (is_accessible(me, symbol, meaning, symbol_table_get(locals, export_name(symbol)), nullptr, false)) {
        out->len = 0;
        chain_push(out, symbol);
        return true;
    }
    // A duplicate declaration (TS2300) has a symbol of its own that isn't in the exports, but the block's local of
    // that name has it as its exportSymbol (not an `export default` one: tsc looks it up as `default`, which no local is;
    // nor an exported alias, `export import A = b.c`, which tsc's declareModuleMember declares in the exports only)
    if (!symbol->is_default_export && !(symbol->alias && symbol->alias->item && symbol->alias->item->export_import) && merged(me, symbol->parent) == merged(me, scope_table->container) && declared_at(symbol, scope_table->file) &&
        merged(me, symbol_table_get(exports, symbol->name)) != merged(me, symbol) &&
        can_qualify(me, symbol, meaning)
    ) {
        out->len = 0;
        chain_push(out, symbol);
        return true;
    }
    u32 count = locals->count + exports->count;
    LocalEntry* entries = malloc(sizeof(LocalEntry) * (count ? count : 1));
    u32 n = 0;
    for (u32 i = 0; i < locals->count; i++) {
        char const* at = declared_at(locals->entries[i].symbol, scope_table->file);
        entries[n++] = (LocalEntry){ &locals->entries[i], at ? at : "", false,
            at && declared_as_function(locals->entries[i].symbol, scope_table->file) };
    }
    for (u32 i = 0; i < exports->count; i++) {
        SymbolEntry const* entry = &exports->entries[i];
        if (csview_equals(symbol_entry_name(entry), "default") || csview_equals(symbol_entry_name(entry), "export=")) continue;
        if (is_export_specifier(entry->symbol) || has_port_tag(entry->symbol, PORT_REEXPORT_GATHER)) continue;
        char const* at = declared_at(entry->symbol, scope_table->file);
        if (!at) continue;
        entries[n++] = (LocalEntry){ entry, at, true, declared_as_function(entry->symbol, scope_table->file) };
    }
    qsort(entries, n, sizeof(LocalEntry), compare_local_entries);
    bool ret = false;
    SymbolChain candidate;
    for (u32 i = 0; i < n; i++) {
        SymbolEntry const* entry = entries[i].entry;
        if (entries[i].exported) {
            // (the local's exportSymbol)
            if (csview_equals_sv(symbol_entry_name(entry), symbol->name) &&
                is_accessible(me, symbol, meaning, merged(me, entry->symbol), nullptr, false)
            ) {
                candidate.len = 0;
                chain_push(&candidate, symbol);
                keep_best_chain(out, &ret, &candidate);
            }
        } else if (try_entry(me, symbol, meaning, symbol_entry_name(entry), entry->symbol, false, true, &candidate)) {
            keep_best_chain(out, &ret, &candidate);
        }
    }
    free(entries);
    return ret;
}

// tsc's trySymbolTable
static bool try_table(
    ChainCtx* me, Symbol const* symbol, u32 meaning, SymbolTable const* table, bool ignore_qualification,
    bool local_lookup, SymbolChain* out
) {
    // Directly by its name
    if (is_accessible(me, symbol, meaning, symbol_table_get(table, export_name(symbol)), nullptr, ignore_qualification)) {
        out->len = 0;
        chain_push(out, symbol);
        return true;
    }
    // Through an alias in scope. (The tables a table overlays belong to the shared lib program, which has none)
    AliasIndex const* aliases = alias_index_of(table);
    u32 count = aliases ? aliases->alias_count : table->count;
    bool found = false;
    SymbolChain candidate;
    for (u32 j = 0; j < count; j++) {
        SymbolEntry const* entry = &table->entries[aliases ? aliases->aliases[j] : j];
        if (try_entry(me, symbol, meaning, symbol_entry_name(entry), entry->symbol, ignore_qualification, local_lookup, &candidate)) {
            keep_best_chain(out, &found, &candidate);
        }
    }
    if (found) return true;
    // Nothing in the globals: `globalThis` is an alias of them (a global shadowed here is `globalThis.X`). Its exports
    // are another table to tsc (getExportsOfSymbol), not one being visited
    Symbol const* global_this = my binder->global_this;
    if (table == &my binder->globals && global_this && !my in_global_this) {
        my in_global_this = true;
        SymbolChain sub;
        bool found = try_table(me, symbol, meaning, table, true, false, &sub) &&
            can_qualify(me, global_this, left_meaning(meaning));
        my in_global_this = false;
        if (found) {
            out->len = 0;
            chain_push(out, global_this);
            for (u32 i = 0; i < sub.len; i++) chain_push(out, sub.items[i]);
            return true;
        }
    }
    return false;
}

static bool accessible_chain_from_table(
    ChainCtx* me, Symbol const* symbol, u32 meaning, SymbolTable const* table, bool ignore_qualification,
    bool local_lookup, SymbolChain* out
) {
    for (u32 i = 0; i < my visited_count; i++) {
        if (my visited[i].symbol == symbol && my visited[i].table == table) return false;
    }
    if (my visited_count == sizeof(my visited) / sizeof(*my visited)) return false;
    my visited[my visited_count++] = (VisitedTable){ symbol, table };
    bool ret = try_table(me, symbol, meaning, table, ignore_qualification, local_lookup, out);
    my visited_count--;
    return ret;
}

static bool accessible_chain_uncached(ChainCtx* me, Symbol const* symbol, u32 meaning, SymbolChain* out) {
    for (u32 i = 0; i < my table_count; i++) {
        ScopeTable const* table = &my tables[i];
        if (table->exports) {
            if (try_locals_with_exports(me, symbol, meaning, table, out)) return true;
            continue;
        }
        if (accessible_chain_from_table(me, symbol, meaning, table->table, false, table->local_lookup, out)) {
            return true;
        }
    }
    return false;
}

typedef struct ChainMemo {
    bool found;
    SymbolChain chain;
} ChainMemo;

// tsc's getAccessibleSymbolChain (members of classes and interfaces aren't Symbols here: they never get here).
// Remembered per (symbol, location, meaning) like tsc's accessibleChainCache, unless it's asked in the middle of a
// search (visited tables, globalThis), whose answer depends on the search. Out is only written when found
static bool accessible_chain(ChainCtx* me, Symbol const* symbol, u32 meaning, SymbolChain* out) {
    if (!symbol) return false;
    Checker* checker = current_checker;
    bool cacheable = checker && !my visited_count && !my in_global_this;
    u64 key[4] = { KEY_ACCESSIBLE_CHAIN, (u64)(uintptr_t)my enclosing, (u64)(uintptr_t)symbol,
        (u64)meaning | (u64)my container_lookup << 32 | (u64)my only_external_aliasing << 33 };
    if (cacheable) {
        ChainMemo const* memo = intern_get(&checker->links, key, 4);
        if (memo) {
            if (memo->found) *out = memo->chain;
            return memo->found;
        }
    }
    SymbolChain chain = {0};
    bool found = accessible_chain_uncached(me, symbol, meaning, &chain);
    if (found) *out = chain;
    if (cacheable) {
        ChainMemo* memo = arena_new(&checker->arena, ChainMemo);
        memo->found = found;
        memo->chain = chain;
        *intern_slot(checker, &checker->links, key, 4) = memo;
    }
    return found;
}

// tsc's needsQualification
static bool needs_qualification(ChainCtx* me, Symbol const* symbol, u32 meaning) {
    if (!meaning) return false;
    for (u32 i = 0; i < my table_count; i++) {
        Symbol const* found = symbol_table_get(my tables[i].table, export_name(symbol));
        if (!found) continue;
        found = merged(me, found);
        // (A class's exports have its `prototype` property in tsc, which a namespace's `prototype` merged into the
        // class doesn't replace: that one is a duplicate, TS2300, which the name means as a value only through the
        // block's local)
        if ((meaning & SYMBOL_VALUE) && found->parent && (merged(me, found->parent)->flags & SYMBOL_CLASS) &&
            csview_equals(found->name, "prototype") && !found->is_local
        ) {
            return true;
        }
        if (found == merged(me, symbol)) return false;
        bool resolve = (found->flags & SYMBOL_ALIAS) && !is_export_specifier(found);
        u32 flags = resolve ? symbol_resolved_flags(found) : found->flags;
        // (An import that resolves to nothing is tsc's unknownSymbol, a Property: only a value meaning)
        if (resolve) {
            Symbol const* target = symbol_resolve_alias((Symbol*)found);
            if (target && target->is_placeholder) flags = SYMBOL_PROPERTY;
        }
        if (flags & meaning) return true;
    }
    return false;
}

// The module (file) a symbol is declared directly in, if any; or the ambient module (`declare module "m" {}`) whose
// block declares it (tsc's getContainersOfSymbol: a module block's module that `export =`s it, checked by the caller)
static Symbol const* declaring_module(Symbol const* symbol) {
    if (!symbol->decl_count) return nullptr;
    Scope const* block = symbol->decls[0].block;
    if (block && block->kind == SCOPE_NAMESPACE && block->container && block->container->is_module &&
        !block->container->module->file
    ) {
        return symbol_table_get(scope_locals(block), symbol->name) == symbol ? block->container : nullptr;
    }
    SourceFile const* file = span_source(symbol->decls[0].name);
    if (!file || !file->is_module || !file->scope || !file->scope->container) return nullptr;
    Symbol const* module = file->scope->container;
    if (symbol_table_get(scope_locals(file->scope), symbol->name) != symbol) return nullptr;
    return module;
}

// The name tsc's symbol has: `default` for an `export default` declaration (whose symbol tttt names like the
// declaration)
static csview export_name(Symbol const* symbol) {
    return symbol->is_default_export ? c_sv("default") : symbol->name;
}

// tsc's getAliasForSymbolInContainer
static Symbol const* alias_in_container(ChainCtx* me, Symbol const* container, Symbol const* symbol) {
    if (merged(me, container) == merged(me, symbol->parent)) return symbol;
    Symbol const* members = symbol_members(container);
    if (!members) return nullptr;
    Symbol const* export_equals = members->exports ? symbol_table_get(members->exports, c_sv("export=")) : nullptr;
    if (export_equals && same_reference(me, export_equals, symbol)) return container;
    // (A module defined by an `export =` has the exports of what it assigns: tsc's getExportsOfModuleWorker, so a
    // `declare namespace rules { export type { Docs } }` merged with the assigned `rules` exports the file's Docs)
    if (export_equals) {
        Symbol const* assigned = module_export_equals((Symbol*)members);
        if (assigned && assigned != members && !(assigned->flags & SYMBOL_ALIAS)) members = symbol_members(assigned);
    }
    Symbol const* quick = symbol_get_export(members, symbol->name);
    if (quick && same_reference(me, quick, symbol)) return quick;
    u32 count;
    SymbolEntry* entries = symbol_collect_exports(members, &count);
    Symbol const* ret = nullptr;
    for (u32 i = 0; i < count && !ret; i++) {
        if (same_reference(me, entries[i].symbol, symbol)) ret = entries[i].symbol;
    }
    free(entries);
    return ret;
}

// The module whose `export =` is the container (tsc's getFileSymbolIfFileSymbolExportEqualsContainer)
static Symbol const* file_symbol_exporting(ChainCtx* me, Symbol const* container) {
    if (!container->decl_count) return nullptr;
    SourceFile const* file = span_source(container->decls[0].name);
    if (!file || !file->is_module || !file->scope || !file->scope->container) return nullptr;
    Symbol const* module = file->scope->container;
    Symbol const* export_equals = module->exports ? symbol_table_get(module->exports, c_sv("export=")) : nullptr;
    return export_equals && same_reference(me, export_equals, container) ? module : nullptr;
}

#define CONTAINERS_MAX 8

// tsc's getAlternativeContainingModules: the modules the enclosing file imports that export the symbol (perhaps through
// `export *`), else every module of the program that does. Remembered per symbol during a declaration walk, which is of
// one file (tsc caches it per symbol and file)
typedef struct ExtendedContainers {
    Symbol const* symbol;
    u32 generation;
    u32 count;
    Symbol const* items[CONTAINERS_MAX];
} ExtendedContainers;
#define EXTENDED_CONTAINER_SLOTS 256
static thread_local ExtendedContainers* extended_containers;

// The modules of the program that export the symbol (not where it's named from): remembered per symbol on each thread
typedef struct ProgramContainers {
    Symbol const* symbol;
    u32 program;
    u32 count;
    Symbol const* items[CONTAINERS_MAX];
} ProgramContainers;
#define PROGRAM_CONTAINER_SLOTS 1024
static thread_local ProgramContainers* program_containers;

static u32 program_modules_exporting(ChainCtx* me, Symbol const* symbol, Symbol const** out, u32 cap) {
    Program const* program = current_checker->program;
    if (!program_containers) program_containers = calloc(PROGRAM_CONTAINER_SLOTS, sizeof(ProgramContainers));
    ProgramContainers* slot = &program_containers[((uintptr_t)symbol >> 4) % PROGRAM_CONTAINER_SLOTS];
    u32 serial = program_serial(program);
    if (slot->symbol != symbol || slot->program != serial) {
        *slot = (ProgramContainers){ .symbol = symbol, .program = serial };
        u32 files = program_source_count(program);
        for (u32 i = 0; i < files && slot->count < CONTAINERS_MAX; i++) {
            SourceFile const* other = program_source_at((Program*)program, i);
            if (!other->is_module || !other->scope || !other->scope->container) continue;
            Symbol const* module = other->scope->container;
            if (alias_in_container(me, module, symbol)) slot->items[slot->count++] = module;
        }
    }
    u32 count = slot->count < cap ? slot->count : cap;
    memcpy(out, slot->items, count * sizeof(*out));
    return count;
}

static u32 alternative_modules_uncached(ChainCtx* me, SourceFile const* file, Symbol const* symbol, Symbol const** out, u32 cap) {
    u32 count = 0;
    for (u32 i = 0; i < file->module_ref_count && count < cap; i++) {
        ModuleRef const* ref = file->module_refs[i];
        // (not the `declare module`s, nor the imports tsc synthesizes)
        if (ref->is_declaration || ref->ambient_only || ref == file->helpers_ref || ref == file->jsx_runtime_ref) continue;
        if (ref->module_state != ALIAS_DONE || !ref->module) continue;
        if (alias_in_container(me, ref->module, symbol)) out[count++] = ref->module;
    }
    if (count || !current_checker) return count;
    return program_modules_exporting(me, symbol, out, cap);
}

static u32 alternative_modules(ChainCtx* me, Symbol const* symbol, Symbol const** out, u32 cap) {
    SourceFile const* file = my enclosing ? my enclosing->file : nullptr;
    if (!file || !cap) return 0;
    // (a walk is of one file)
    if (!alias_index_generation) return alternative_modules_uncached(me, file, symbol, out, cap);
    if (!extended_containers) extended_containers = calloc(EXTENDED_CONTAINER_SLOTS, sizeof(ExtendedContainers));
    ExtendedContainers* slot = &extended_containers[((uintptr_t)symbol >> 4) % EXTENDED_CONTAINER_SLOTS];
    if (slot->symbol != symbol || slot->generation != alias_index_generation) {
        *slot = (ExtendedContainers){ .symbol = symbol, .generation = alias_index_generation };
        slot->count = alternative_modules_uncached(me, file, symbol, slot->items, CONTAINERS_MAX);
    }
    u32 count = slot->count < cap ? slot->count : cap;
    memcpy(out, slot->items, count * sizeof(*out));
    return count;
}

// getWithAlternativeContainers: the container (first if it can be named from here), a module that `export =`s it, and
// the modules that re-export the symbol
static u32 with_alternative_containers(ChainCtx* me, Symbol const* symbol, Symbol const* container, u32 meaning, Symbol const** out) {
    u32 count = 0;
    Symbol const* additional = file_symbol_exporting(me, container);
    SymbolChain ignored;
    if ((container->flags & left_meaning(meaning)) && accessible_chain(me, container, SYMBOL_NAMESPACE, &ignored)) {
        out[count++] = container;
        if (additional) out[count++] = additional;
    } else {
        if (additional) out[count++] = additional;
        out[count++] = container;
    }
    count += alternative_modules(me, symbol, out + count, CONTAINERS_MAX - count);
    return count;
}

// The class expressions whose bodies symbols are being named from on this thread (symbol_chain_enter_class())
static thread_local ClassDecl const* inside_classes[16];
static thread_local u32 inside_class_count;

void symbol_chain_enter_class(ClassDecl const* class_decl) {
    if (inside_class_count < c_arraylen(inside_classes)) inside_classes[inside_class_count] = class_decl;
    inside_class_count++;
}

void symbol_chain_leave_class(void) {
    if (inside_class_count) inside_class_count--;
}

static bool is_inside_class(ClassDecl const* class_decl) {
    for (u32 i = 0; i < inside_class_count && i < c_arraylen(inside_classes); i++) {
        if (inside_classes[i] == class_decl) return true;
    }
    return false;
}

// A named class expression whose body symbols are named from: its own name is in scope there
static bool is_inside_class_of(Symbol const* symbol) {
    if (!inside_class_count || !(symbol->flags & SYMBOL_CLASS)) return false;
    ClassDecl const* class_expression = symbol_find_node(symbol, SYMBOL_DECL_CLASS);
    return class_expression && class_expression->is_expression && span_valid(class_expression->name) && is_inside_class(class_expression);
}

// tsc's getContainersOfSymbol
static u32 containers_of(ChainCtx* me, Symbol const* symbol, u32 meaning, Symbol const** out) {
    ClassDecl const* class_expression = symbol->flags & SYMBOL_CLASS ? symbol_find_node(symbol, SYMBOL_DECL_CLASS) : nullptr;
    Symbol const* container = symbol->parent ? merged(me, symbol->parent) : nullptr;
    if (container && !(symbol->flags & SYMBOL_TYPE_PARAMETER)) return with_alternative_containers(me, symbol, container, meaning, out);
    // A JavaScript class expression a CommonJS module exports (`module.exports = class {}`, `exports.C = class {}`),
    // unless the module's typedefs made its `export=` a namespace of its own (tsgo's resolveExternalModuleSymbol
    // doesn't resolve it then)
    Symbol const* export_equals = class_expression && class_expression->is_expression && class_expression->commonjs_module ?
        symbol_get_export((Symbol*)class_expression->commonjs_module, c_sv("export=")) : nullptr;
    if (class_expression && class_expression->is_expression && class_expression->commonjs_module &&
        !(export_equals && (export_equals->flags & SYMBOL_ALIAS) && (export_equals->flags & SYMBOL_NAMESPACE_MODULE))
    ) {
        out[0] = class_expression->commonjs_module;
        return 1;
    }
    // A declaration directly in a module that the module exports under another name (`export = C`, `export { C as D }`)
    Symbol const* module = declaring_module(symbol);
    if (module && alias_in_container(me, module, symbol)) return with_alternative_containers(me, symbol, module, meaning, out);
    return 0;
}

static thread_local ModuleSpecifierFn module_specifier_fn;
static thread_local void* module_specifier_ctx;

void symbol_chain_set_module_specifier(ModuleSpecifierFn fn, void* ctx) {
    module_specifier_fn = fn;
    module_specifier_ctx = ctx;
}

// getSpecifierForModuleSymbol of a container, empty if it isn't a module
static cstr parent_specifier(Symbol const* parent, SourceFile const* from) {
    if (!parent->is_module || !parent->module) return cstr_null;
    SourceFile const* file = parent->module->file;
    if (!file) {
        // An ambient module by its name
        csview name = parent->name;
        if (name.size >= 2 && (name.buf[0] == '"' || name.buf[0] == '\'')) name = csview_slice(name, 1, name.size - 1);
        return cstr_from_sv(name);
    }
    if (module_specifier_fn) return module_specifier_fn(module_specifier_ctx, file);
    // (tsc 6 always has a host where there is an enclosing file: the specifier from there)
    if (from && current_checker) {
        return program_module_specifier(current_checker->program, from, file, SPECIFIER_MODE_NONE);
    }
    // (without one: the file's name, absolute; the files of a program are all below its directory, or have `..`s
    // that count as components alike)
    return cstr_d("/", file->canonical_path);
}

// moduleSpecifiers.countPathComponents
static u32 path_components(csview path) {
    u32 count = 0;
    for (isize i = csview_starts_with(path, "./") ? 2 : 0; i < path.size; i++) count += path.buf[i] == '/';
    return count;
}

static bool specifier_is_relative(csview path) {
    return csview_starts_with(path, "./") || csview_starts_with(path, "../") || csview_equals(path, ".") ||
        csview_equals(path, "..");
}

// tsgo's getSymbolChain sorting by sortByBestName: modules with fewer path components first, non-relative before
// relative, anything else by compareSymbols (a module that `export =`s the container is declared at its file's start,
// so it comes before the container declared in it). Go's SortStableFunc on so few is an insertion sort, which this
// is (the comparison isn't a total order, so the algorithm shows)
static void sort_by_best_name(Symbol const** parents, u32 count, SourceFile const* from) {
    if (count < 2) return;
    cstr specifiers[CONTAINERS_MAX];
    for (u32 i = 0; i < count; i++) specifiers[i] = parent_specifier(parents[i], from);
    for (u32 i = 1; i < count; i++) {
        for (u32 j = i; j > 0; j--) {
            csview a = cstr_sv(&specifiers[j]), b = cstr_sv(&specifiers[j - 1]);
            int compared;
            if (a.size && b.size) {
                bool a_relative = specifier_is_relative(a), b_relative = specifier_is_relative(b);
                compared = a_relative == b_relative ? (int)path_components(a) - (int)path_components(b) : b_relative ? -1 : 1;
            } else {
                compared = ty_compare_symbols(parents[j], parents[j - 1]);
            }
            if (compared >= 0) break;
            c_swap(&parents[j], &parents[j - 1]);
            c_swap(&specifiers[j], &specifiers[j - 1]);
        }
    }
    for (u32 i = 0; i < count; i++) cstr_drop(&specifiers[i]);
}

// tsc's getSymbolChain
static bool is_inside_class_of(Symbol const* symbol);

static bool symbol_chain(ChainCtx* me, Symbol const* symbol, u32 meaning, bool end_of_chain, u32 depth, SymbolChain* out) {
    SymbolChain chain = {0};
    if (depth == 0 && is_inside_class_of(symbol)) {
        chain_push(out, symbol);
        return true;
    }
    bool have = accessible_chain(me, symbol, meaning, &chain);
    if (depth < 8 && (!have || needs_qualification(me, chain.items[0], chain.len == 1 ? meaning : left_meaning(meaning)))) {
        Symbol const* parents[CONTAINERS_MAX];
        u32 parent_count = containers_of(me, have ? chain.items[0] : symbol, meaning, parents);
        sort_by_best_name(parents, parent_count, my enclosing ? my enclosing->file : nullptr);
        for (u32 i = 0; i < parent_count; i++) {
            SymbolChain parent_chain = {0};
            bool was_container_lookup = my container_lookup;
            my container_lookup = true;
            bool found = symbol_chain(me, parents[i], left_meaning(meaning), false, depth + 1, &parent_chain);
            my container_lookup = was_container_lookup;
            if (!found) continue;
            Symbol const* members = symbol_members(parents[i]);
            Symbol const* export_equals = members && members->exports ? symbol_table_get(members->exports, c_sv("export=")) : nullptr;
            if (export_equals && same_reference(me, export_equals, symbol)) {
                chain = parent_chain;
                have = true;
                break;
            }
            if (!have) {
                chain.len = 0;
                Symbol const* alias = alias_in_container(me, parents[i], symbol);
                chain_push(&chain, alias ? alias : symbol);
            }
            for (u32 j = parent_chain.len; j-- > 0;) chain_prepend(&chain, parent_chain.items[j]);
            have = true;
            break;
        }
    }
    if (have) {
        *out = chain;
        return true;
    }
    if (!end_of_chain && !my yield_module && is_external_module_symbol(symbol)) return false;
    out->len = 0;
    chain_push(out, symbol);
    return true;
}

SymbolChain symbol_chain_at(Scope const* enclosing, Symbol const* symbol, u32 meaning, bool yield_module) {
    SymbolChain ret = {0};
    if (!enclosing) {
        chain_push(&ret, symbol);
        return ret;
    }
    ChainCtx* ctx = malloc(sizeof(ChainCtx));
    chain_ctx_init(ctx, enclosing);
    ctx->yield_module = yield_module;
    symbol_chain(ctx, symbol, meaning, true, 0, &ret);
    free(ctx);
    return ret;
}

SymbolChain symbol_member_container_chain_at(Scope const* enclosing, Symbol const* container) {
    SymbolChain ret = {0};
    if (!enclosing || !container) return ret;
    ChainCtx* ctx = malloc(sizeof(ChainCtx));
    chain_ctx_init(ctx, enclosing);
    if (!symbol_chain(ctx, container, SYMBOL_NAMESPACE, false, 0, &ret)) ret.len = 0;
    free(ctx);
    return ret;
}

// Printing (tsc's getNameOfSymbolAsWritten and createExpressionFromSymbolChain)

// Of the UTF-16 code unit at c, as tsc's charCodeAt sees it: half of a surrogate pair starts nothing
static bool is_identifier_start(const char* c, const char* end) {
    unsigned char b = (unsigned char)*c;
    if (b < 0x80) return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || b == '_' || b == '$';
    if (b >= 0xF0) return false;
    u32 code = b >= 0xE0 && end - c >= 3 ? (u32)(b & 0x0F) << 12 | (u32)(c[1] & 0x3F) << 6 | (c[2] & 0x3F)
        : end - c >= 2 ? (u32)(b & 0x1F) << 6 | (c[1] & 0x3F) : 0;
    return unicode_is_identifier_start(code);
}

// tsgo's scanner.IsIdentifierText: every code point (decoded from UTF-8) is an identifier start, then part. An escape
// (`x\u0078` as written) isn't
static bool is_identifier_text(csview name) {
    if (!name.size) return false;
    for (isize i = 0; i < name.size;) {
        unsigned char b = (unsigned char)name.buf[i];
        u32 len = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
        if (i + len > name.size) return false;
        u32 code = len == 1 ? b : len == 2 ? (u32)(b & 0x1F) : len == 3 ? (u32)(b & 0x0F) : (u32)(b & 0x07);
        for (u32 k = 1; k < len; k++) code = code << 6 | ((unsigned char)name.buf[i + k] & 0x3F);
        bool valid = code < 0x80 ?
            (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') || code == '_' || code == '$' || (i > 0 && code >= '0' && code <= '9') :
            i == 0 ? unicode_is_identifier_start(code) : unicode_is_identifier_part(code);
        if (!valid) return false;
        i += len;
    }
    return true;
}

// tsgo's canUsePropertyAccess (the whole name, where 6.0 looked at its first character)
static bool can_use_property_access(csview name) {
    if (!name.size) return false;
    if (name.buf[0] == '#') return name.size > 1 && is_identifier_text(csview_slice(name, 1, name.size));
    return is_identifier_text(name);
}

static bool is_quote(char c) {
    return c == '"' || c == '\'';
}

static SourceFile const* file_of_scope(Scope const* scope) {
    return scope ? scope->file : nullptr;
}

// tsc's getNameOfSymbolAsWritten
static csview written_name(Scope const* enclosing, Symbol const* symbol, bool first) {
    // A late bound `f[k] = v` is named by the whole access (tsgo's GetNonAssignedNameOfDeclaration: the element access
    // whose argument isn't a literal), `x.f[f[k]]` in a chain
    if (symbol_rare(symbol)->late_bound_key && symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_ASSIGNMENT &&
        span_valid(symbol->decls[0].name)
    ) {
        return span_sv(symbol->decls[0].name);
    }
    Span display = ty_symbol_display_name(symbol);
    csview name = span_valid(display) ? span_sv(display) : symbol->name;
    // An `export default` declaration is `default` except where it was declared
    if (csview_equals(export_name(symbol), "default") &&
        (!first || !span_valid(symbol->name_span) || span_source(symbol->name_span) != file_of_scope(enclosing))
    ) {
        return c_sv("default");
    }
    // A declaration whose name is missing (`enum void {}`: tsc's missing identifier)
    if (!name.size && span_valid(symbol->name_span) && !symbol->name_span.size) return c_sv("(Missing)");
    return name;
}

void symbol_chain_append_member(cstr* out, csview name, bool is_enum_member) {
    if (!name.size) {
        cstr_append_lit(out, "[(Missing)]"); // A member whose name is missing (tsc's "__missing")
        return;
    }
    if (can_use_property_access(name)) {
        cstr_append_lit(out, ".");
        cstr_append_sv(out, name);
        return;
    }
    if (name.size >= 2 && name.buf[0] == '[' && name.buf[name.size - 1] == ']') name = csview_slice(name, 1, name.size - 1);
    cstr_append_lit(out, "[");
    if (name.size && is_quote(name.buf[0]) && !is_enum_member) {
        // A string literal made from the name: quotes kept, backslash escapes dropped
        char quote = name.buf[0];
        csview inner = name.size >= 2 && name.buf[name.size - 1] == quote ? csview_slice(name, 1, name.size - 1) : csview_slice(name, 1, name.size);
        cstr_push(out, quote == '\'' ? "'" : "\"");
        for (isize i = 0; i < inner.size; i++) {
            if (inner.buf[i] == '\\' && i + 1 < inner.size) i++;
            // (the printer escapes the text again)
            if (inner.buf[i] == '\\' || inner.buf[i] == quote) cstr_push(out, "\\");
            unsigned char b = (unsigned char)inner.buf[i];
            if (b < 0x80) {
                cstr_append_n(out, &inner.buf[i], 1);
                continue;
            }
            // (non-ASCII as \uXXXX UTF-16 escapes, like the printer does a string literal node it made)
            u32 len = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
            if (i + len > inner.size) len = 1;
            u32 code = len == 1 ? b : b & (0xFF >> (len + 1));
            for (u32 k = 1; k < len; k++) code = code << 6 | ((unsigned char)inner.buf[i + k] & 0x3F);
            i += len - 1;
            char buf[16];
            if (code >= 0x10000) {
                code -= 0x10000;
                snprintf(buf, sizeof buf, "\\u%04X\\u%04X", 0xD800 + (code >> 10), 0xDC00 + (code & 0x3FF));
            } else {
                snprintf(buf, sizeof buf, "\\u%04X", code);
            }
            cstr_append(out, buf);
        }
        cstr_push(out, quote == '\'' ? "'" : "\"");
    } else {
        cstr_append_sv(out, name);
    }
    cstr_append_lit(out, "]");
}

void symbol_chain_append(cstr* out, Scope const* enclosing, SymbolChain const* chain) {
    symbol_chain_append_entity(out, enclosing, chain, false, false);
}

// An expando's container in tsgo is the initializer's symbol of its host (`var x = function f() {}; x.a = 1`), which
// has no parent: a chain to the member starts there, named by the expression (`f.a`) or else the variable
static u32 expando_initializer_start(SymbolChain const* chain, Span* host_name) {
    *host_name = span_null;
    for (u32 i = chain->len - 1; i-- > 0;) {
        Symbol const* next = chain->items[i + 1];
        if (next->parent != chain->items[i] || !next->decl_count || next->decls[0].kind != SYMBOL_DECL_ASSIGNMENT) continue;
        if (expando_host_is_initializer(chain->items[i], host_name)) return i;
    }
    return 0;
}

void symbol_chain_append_entity(cstr* out, Scope const* enclosing, SymbolChain const* chain, bool entity, bool after_module) {
    Span host_name = span_null;
    u32 start = chain->len > 1 ? expando_initializer_start(chain, &host_name) : 0;
    if (start) after_module = false;
    for (u32 i = start; i < chain->len; i++) {
        Symbol const* symbol = chain->items[i];
        bool first = i == start;
        csview name = written_name(enclosing, symbol, first && !after_module);
        if (first && span_valid(host_name) && i + 1 < chain->len) name = span_sv(host_name);
        // (An entity name has no element accesses: a quoted name that is an identifier is written as one, tsc's
        // symbolToTypeNode of an `o["x"] = v` expando is `typeof o.x`)
        // (Not a module's export, `exports["d"] = v`: the binder declares it by the literal, which getNameOfSymbolAsWritten
        // writes as it is, an identifier `"d"`, where an expando of an object is late bound by its name type)
        bool quoted = entity && !first && name.size >= 2 && is_quote(name.buf[0]) && name.buf[name.size - 1] == name.buf[0];
        if (quoted && symbol->parent && symbol->parent->is_module) {
            cstr_append_lit(out, ".");
            cstr_append_sv(out, name);
            continue;
        }
        if (quoted) {
            csview inner = csview_slice(name, 1, name.size - 1);
            bool plain = can_use_property_access(inner);
            for (isize k = 0; plain && k < inner.size; k++) plain = inner.buf[k] != '\\' && inner.buf[k] != '-' && inner.buf[k] != ' ';
            if (plain) name = inner;
        }
        if (first) {
            cstr_append_sv(out, name);
        } else {
            symbol_chain_append_member(out, name, symbol->flags & SYMBOL_ENUM_MEMBER);
        }
    }
}

// Accessibility (tsc's isSymbolAccessible, hasVisibleDeclarations, isDeclarationVisible)

// The declaration of the namespace whose block a scope is
static SymbolDecl const* namespace_decl_of_scope(Scope const* scope) {
    Symbol const* ns = scope->container;
    if (!ns) return nullptr;
    for (u32 i = 0; i < ns->decl_count; i++) {
        if (ns->decls[i].kind == SYMBOL_DECL_NAMESPACE && ((NamespaceDecl const*)ns->decls[i].node)->inner_scope == scope) {
            return &ns->decls[i];
        }
    }
    return nullptr;
}

static bool statements_visible(Scope const* scope, u32 depth);

static thread_local Symbol const* const* const* painted_symbols;
static thread_local u32 const* painted_count;

// During a declaration walk (painted_symbols set), the accessibility answers so far: the printer asks about the same
// symbols from the same scope over and over. Painting more may change them, so it starts over then
typedef struct AccessMemo {
    Scope const* enclosing;
    Symbol const* symbol;
    u32 meaning;
    bool allow_modules;
    u32 generation; // Valid in this generation only
    SymbolAccess access;
} AccessMemo;

#define ACCESS_MEMO_SIZE 1024
static thread_local AccessMemo* access_memo;
static thread_local u32 access_memo_painted;
static thread_local u32 access_memo_generation;

void symbol_chain_set_painted(Symbol const* const* const* symbols, u32 const* count) {
    painted_symbols = symbols;
    painted_count = count;
    if (symbols && !access_memo) access_memo = calloc(ACCESS_MEMO_SIZE, sizeof(AccessMemo));
    access_memo_generation++;
    static thread_local u32 walks;
    if (symbols && !++walks) walks = 1; // (0 is "not in a walk")
    alias_index_generation = symbols ? walks : 0;
    access_memo_painted = 0;
}

void symbol_chain_release_thread(void) {
    free(extended_containers);
    extended_containers = nullptr;
    free(program_containers);
    program_containers = nullptr;
    free(access_memo);
    access_memo = nullptr;
}

static AccessMemo* access_memo_slot(Scope const* enclosing, Symbol const* symbol, u32 meaning, bool allow_modules) {
    if (!painted_symbols) return nullptr;
    if (*painted_count != access_memo_painted) {
        access_memo_generation++;
        access_memo_painted = *painted_count;
    }
    uintptr_t hash = ((uintptr_t)symbol >> 4) * 31 + ((uintptr_t)enclosing >> 4) * 17 + meaning * 7 + allow_modules;
    return &access_memo[hash % ACCESS_MEMO_SIZE];
}

static thread_local Symbol const* const* kept_painted;
static thread_local u32 kept_painted_count;

void symbol_chain_set_kept_painted(Symbol const* const* symbols, u32 count) {
    kept_painted = symbols;
    kept_painted_count = count;
}

static bool is_painted(Symbol const* symbol) {
    for (u32 i = 0; i < kept_painted_count; i++) {
        if (kept_painted[i] == symbol) return true;
    }
    if (!painted_symbols) return false;
    for (u32 i = 0; i < *painted_count; i++) {
        if ((*painted_symbols)[i] == symbol) return true;
    }
    return false;
}

// `export = x`, `export default x` or `export { x }` in the module or namespace makes x's declarations visible (tsc's
// collectLinkedAliases)
// (Only in a file that is emitted: collectLinkedAliases runs from the declaration transform of the `export =`, never
// for a declaration file's: a namespace only `export =` names in a .d.ts stays invisible, TS4060 for a name of it)
static bool exported_by_alias(Scope const* scope, Symbol const* symbol) {
    if ((scope->kind != SCOPE_MODULE && scope->kind != SCOPE_NAMESPACE) || !scope->container) return false;
    if (scope->file && scope->file->is_declaration) return false;
    SymbolTable const* exports = symbol_members(scope->container)->exports;
    if (!exports) return false;
    Binder const* binder = scope->binder;
    Symbol const* target = binder_redirect(binder, symbol);
    for (u32 i = 0; i < exports->count; i++) {
        Symbol const* entry = exports->entries[i].symbol;
        if (!entry || !(entry->flags & SYMBOL_ALIAS) || !entry->alias) continue;
        u32 tag = entry->alias->item ? entry->alias->item->tag : PORT_EXPORT_SPECIFIC;
        if (tag != PORT_EXPORT_SPECIFIC) continue; // (not an import or re-export)
        Symbol const* resolved = symbol_resolve_alias((Symbol*)entry);
        if (resolved && binder_redirect(binder, resolved) == target) return true;
    }
    return false;
}

// isDeclarationVisible of a namespace declaration
static bool namespace_visible(Symbol const* ns, SymbolDecl const* decl, u32 depth) {
    NamespaceDecl const* node = decl->node;
    // (an external module augmentation is always visible, `declare module "m"` in a script is a global declaration)
    if (node->is_augmentation || node->is_global) return true;
    Scope const* outer = decl->block;
    // (An ambient module `declare module "m"` in a script isn't declared in a scope: it is global)
    if (!outer) return true;
    if (decl->hoisted) return false;
    bool in_ambient = (outer->kind == SCOPE_NAMESPACE && outer->ambient) || outer->kind == SCOPE_GLOBAL_AUGMENTATION;
    if (ns->exported || in_ambient || exported_by_alias(outer, ns) || is_painted(ns)) return statements_visible(outer, depth + 1);
    return outer->kind == SCOPE_SCRIPT;
}

static bool statements_visible(Scope const* scope, u32 depth) {
    switch (scope->kind) {
        case SCOPE_MODULE:
        case SCOPE_SCRIPT:
        case SCOPE_GLOBAL:
        case SCOPE_GLOBAL_AUGMENTATION:
            return true;
        case SCOPE_NAMESPACE: {
            SymbolDecl const* decl = namespace_decl_of_scope(scope);
            // (not knowing which declaration it is, rather nothing than a spurious diagnostic)
            if (!decl || depth > 32) return true;
            return namespace_visible(scope->container, decl, depth);
        }
        default: return false;
    }
}

bool scope_statements_visible(Scope const* scope) { return statements_visible(scope, 0); }

static bool decl_visible(SymbolDecl const* decl) {
    switch (decl->kind) {
        case SYMBOL_DECL_VARIABLE:
        case SYMBOL_DECL_FUNCTION:
        case SYMBOL_DECL_CLASS:
        case SYMBOL_DECL_INTERFACE:
        case SYMBOL_DECL_ENUM:
        case SYMBOL_DECL_TYPE_ALIAS:
            if (!decl->block) return true;
            if (decl->hoisted) return false;
            return scope_statements_visible(decl->block);
        case SYMBOL_DECL_NAMESPACE: {
            NamespaceDecl const* node = decl->node;
            if (node->is_augmentation || node->is_global) return true;
            if (!decl->block) return true;
            if (decl->hoisted) return false;
            return scope_statements_visible(decl->block);
        }
        case SYMBOL_DECL_ALIAS: {
            PortItem const* item = decl->node;
            // An export specifier or `export default x` isn't visible itself (tsc's default case, ExportAssignment);
            // an import is if its statement list is (shown on demand)
            if (!item) return false;
            switch (item->tag) {
                case PORT_EXPORT_SPECIFIC:
                case PORT_REEXPORT_SPECIFIC:
                case PORT_REEXPORT_GATHER:
                    return false;
                default:
                    return !decl->block || scope_statements_visible(decl->block);
            }
        }
        case SYMBOL_DECL_EXPR: return false; // `export default <expr>`
        default: return true;
    }
}

bool symbol_has_visible_declarations(Symbol const* symbol) {
    // (A parameter is as visible as its function, which is being emitted where it is named)
    if (symbol->is_parameter) return true;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (!decl_visible(&symbol->decls[i])) return false;
    }
    return true;
}

Scope const* scope_external_module(Scope const* scope) {
    for (; scope; scope = scope->parent) {
        if (scope->kind == SCOPE_MODULE || scope->kind == SCOPE_GLOBAL_AUGMENTATION) return scope;
        if (scope->kind == SCOPE_NAMESPACE && scope->container && scope->container->is_module) return scope;
    }
    return nullptr;
}

// tsc's isAnySymbolAccessible (allowModules): whether one of `symbols` is accessible or reachable through its
// containers. `had`: a symbol whose chain was accessible but whose declarations weren't visible
static bool any_accessible(
    ChainCtx* me, Symbol const* const* symbols, u32 count, Symbol const* initial, u32 meaning, bool allow_modules,
    u32 depth, SymbolAccess* out
) {
    Symbol const* had = nullptr;
    bool module_bail = false;
    for (u32 i = 0; i < count; i++) {
        Symbol const* symbol = symbols[i];
        SymbolChain chain;
        if (accessible_chain(me, symbol, meaning, &chain)) {
            had = symbol;
            if (symbol_has_visible_declarations(chain.items[0])) {
                *out = (SymbolAccess){ .kind = SYMBOL_ACCESSIBLE, .head = chain.items[0] };
                return true;
            }
        }
        // (Any meaning of a module is accessible through an `import()` type, but its aliases are tried first)
        if (allow_modules && is_external_module_symbol(symbol)) {
            module_bail = true;
            continue;
        }
        if (depth >= 8) continue;
        Symbol const* parents[CONTAINERS_MAX];
        u32 parent_count = containers_of(me, symbol, meaning, parents);
        u32 parent_meaning = initial == symbol ? left_meaning(meaning) : meaning;
        if (parent_count && any_accessible(me, parents, parent_count, initial, parent_meaning, allow_modules, depth + 1, out)) {
            return true;
        }
    }
    if (module_bail) {
        *out = (SymbolAccess){ .kind = SYMBOL_ACCESSIBLE };
        return true;
    }
    if (had) {
        *out = (SymbolAccess){ .kind = SYMBOL_NOT_ACCESSIBLE, .module = had != initial ? had : nullptr };
        return true;
    }
    return false;
}

// The external module a symbol is declared in (its first declaration that is in one)
static Scope const* symbol_external_module(Symbol const* symbol) {
    for (u32 i = 0; i < symbol->decl_count; i++) {
        Scope const* scope = symbol->decls[i].block ? scope_external_module(symbol->decls[i].block) : nullptr;
        if (scope) return scope;
    }
    return nullptr;
}

static SymbolAccess accessibility_compute(Scope const* enclosing, Symbol const* symbol, u32 meaning, bool allow_modules);

static SymbolAccess accessibility_at(Scope const* enclosing, Symbol const* symbol, u32 meaning, bool allow_modules) {
    if (!enclosing || !symbol) return (SymbolAccess){ .kind = SYMBOL_ACCESSIBLE };
    AccessMemo* slot = access_memo_slot(enclosing, symbol, meaning, allow_modules);
    if (slot && slot->generation == access_memo_generation && slot->symbol == symbol && slot->enclosing == enclosing &&
        slot->meaning == meaning && slot->allow_modules == allow_modules
    ) {
        return slot->access;
    }
    SymbolAccess ret = accessibility_compute(enclosing, symbol, meaning, allow_modules);
    if (slot) {
        *slot = (AccessMemo){
            .enclosing = enclosing, .symbol = symbol, .meaning = meaning, .allow_modules = allow_modules,
            .generation = access_memo_generation, .access = ret,
        };
    }
    return ret;
}

static SymbolAccess accessibility_compute(Scope const* enclosing, Symbol const* symbol, u32 meaning, bool allow_modules) {
    ChainCtx* ctx = malloc(sizeof(ChainCtx));
    chain_ctx_init(ctx, enclosing);
    SymbolAccess ret = {0};
    bool found = any_accessible(ctx, &symbol, 1, symbol, meaning, allow_modules, 0, &ret);
    free(ctx);
    if (found) return ret;
    // A symbol another external module doesn't export (or that nothing here imports), or a local name
    Scope const* module = symbol_external_module(symbol);
    if (module && module != scope_external_module(enclosing)) {
        return (SymbolAccess){ .kind = SYMBOL_CANNOT_BE_NAMED, .module = module->container };
    }
    return (SymbolAccess){ .kind = SYMBOL_NOT_ACCESSIBLE };
}

SymbolAccess symbol_accessibility_at(Scope const* enclosing, Symbol const* symbol, u32 meaning) {
    return accessibility_at(enclosing, symbol, meaning, true);
}

bool symbol_value_accessible_at(Scope const* enclosing, Symbol const* symbol) {
    return accessibility_at(enclosing, symbol, SYMBOL_VALUE, true).kind == SYMBOL_ACCESSIBLE;
}

bool symbol_type_accessible_at(Scope const* enclosing, Symbol const* symbol) {
    return accessibility_at(enclosing, symbol, SYMBOL_TYPE, true).kind == SYMBOL_ACCESSIBLE;
}

bool symbol_accessible_without_modules_at(Scope const* enclosing, Symbol const* symbol, u32 meaning) {
    return accessibility_at(enclosing, symbol, meaning, false).kind == SYMBOL_ACCESSIBLE;
}
