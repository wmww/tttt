#include "symbol_chain.h"
#include "class_decl.h"
#include "unicode_identifiers.h"
#include "compiler_options.h"
#include "port_statement.h"
#include "source_file.h"
#include "ty/ty.h"

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
    // tsc's visitedSymbolTablesMap: the tables being searched for each symbol, so that aliases of each other end
    VisitedTable visited[64];
    u32 visited_count;
} ChainCtx;

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
                add_table(me, &scope->locals, true);
                break;
            case SCOPE_MODULE:
            case SCOPE_NAMESPACE:
                add_locals_with_exports(me, &scope->locals, scope->container, scope->file);
                break;
            case SCOPE_SCRIPT: // Locals of a script are globals
            case SCOPE_GLOBAL_AUGMENTATION:
            case SCOPE_ENUM: // (An enum declaration has no locals: its members aren't in scope for tsc's walk)
                break;
            case SCOPE_GLOBAL:
                break;
        }
        if (scope->kind == SCOPE_MODULE) my in_external_module = true;
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
    return symbol->flags & SYMBOL_ALIAS ? symbol_resolve_alias((Symbol*)symbol) : symbol;
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
    if (named != symbol && identity(me, named) != identity(me, symbol)) return false;
    if (is_external_module_symbol(from_table)) return false;
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
    Symbol const* resolved = resolve_alias(entry);
    if (!resolved) return false;
    return candidate_list(me, symbol, meaning, entry, resolved, ignore_qualification, out);
}

static char const* declared_at(Symbol const* symbol, SourceFile const* file) {
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].name.source == file && span_valid(symbol->decls[i].name)) return symbol->decls[i].name.sv.buf;
    }
    return nullptr;
}

typedef struct LocalEntry {
    SymbolEntry const* entry;
    char const* at;
    bool exported;
} LocalEntry;

static int compare_local_entries(void const* a, void const* b) {
    char const* x = ((LocalEntry const*)a)->at;
    char const* y = ((LocalEntry const*)b)->at;
    return x < y ? -1 : x > y;
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
    // that name has it as its exportSymbol
    if (merged(me, symbol->parent) == merged(me, scope_table->container) && declared_at(symbol, scope_table->file) &&
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
        entries[n++] = (LocalEntry){ &locals->entries[i], at ? at : "", false };
    }
    for (u32 i = 0; i < exports->count; i++) {
        SymbolEntry const* entry = &exports->entries[i];
        if (csview_equals(entry->name, "default") || csview_equals(entry->name, "export=")) continue;
        if (is_export_specifier(entry->symbol) || has_port_tag(entry->symbol, PORT_REEXPORT_GATHER)) continue;
        char const* at = declared_at(entry->symbol, scope_table->file);
        if (!at) continue;
        entries[n++] = (LocalEntry){ entry, at, true };
    }
    qsort(entries, n, sizeof(LocalEntry), compare_local_entries);
    bool ret = false;
    for (u32 i = 0; i < n && !ret; i++) {
        SymbolEntry const* entry = entries[i].entry;
        if (entries[i].exported) {
            // (the local's exportSymbol)
            if (csview_equals_sv(entry->name, symbol->name) &&
                is_accessible(me, symbol, meaning, merged(me, entry->symbol), nullptr, false)
            ) {
                out->len = 0;
                chain_push(out, symbol);
                ret = true;
            }
        } else {
            ret = try_entry(me, symbol, meaning, entry->name, entry->symbol, false, true, out);
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
    for (u32 i = 0; i < table->count; i++) {
        SymbolEntry const* entry = &table->entries[i];
        if (try_entry(me, symbol, meaning, entry->name, entry->symbol, ignore_qualification, local_lookup, out)) {
            return true;
        }
    }
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

// tsc's getAccessibleSymbolChain (members of classes and interfaces aren't Symbols here: they never get here)
static bool accessible_chain(ChainCtx* me, Symbol const* symbol, u32 meaning, SymbolChain* out) {
    if (!symbol) return false;
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

// tsc's needsQualification
static bool needs_qualification(ChainCtx* me, Symbol const* symbol, u32 meaning) {
    if (!meaning) return false;
    for (u32 i = 0; i < my table_count; i++) {
        Symbol const* found = symbol_table_get(my tables[i].table, export_name(symbol));
        if (!found) continue;
        found = merged(me, found);
        if (found == merged(me, symbol)) return false;
        bool resolve = (found->flags & SYMBOL_ALIAS) && !is_export_specifier(found);
        u32 flags = resolve ? symbol_resolved_flags(found) : found->flags;
        if (flags & meaning) return true;
    }
    return false;
}

// The module (file) a symbol is declared directly in, if any
static Symbol const* declaring_module(Symbol const* symbol) {
    if (!symbol->decl_count) return nullptr;
    SourceFile const* file = symbol->decls[0].name.source;
    if (!file || !file->is_module || !file->scope || !file->scope->container) return nullptr;
    Symbol const* module = file->scope->container;
    if (symbol_table_get(&file->scope->locals, symbol->name) != symbol) return nullptr;
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
    SourceFile const* file = container->decls[0].name.source;
    if (!file || !file->is_module || !file->scope || !file->scope->container) return nullptr;
    Symbol const* module = file->scope->container;
    Symbol const* export_equals = module->exports ? symbol_table_get(module->exports, c_sv("export=")) : nullptr;
    return export_equals && same_reference(me, export_equals, container) ? module : nullptr;
}

#define CONTAINERS_MAX 8

// tsc's getContainersOfSymbol (without the alternative containing modules: the modules that re-export the symbol)
static u32 containers_of(ChainCtx* me, Symbol const* symbol, u32 meaning, Symbol const** out) {
    u32 count = 0;
    Symbol const* container = symbol->parent ? merged(me, symbol->parent) : nullptr;
    if (container && !(symbol->flags & SYMBOL_TYPE_PARAMETER)) {
        Symbol const* additional = file_symbol_exporting(me, container);
        SymbolChain ignored;
        if ((container->flags & left_meaning(meaning)) && accessible_chain(me, container, SYMBOL_NAMESPACE, &ignored)) {
            out[count++] = container;
            if (additional) out[count++] = additional;
        } else {
            if (additional) out[count++] = additional;
            out[count++] = container;
        }
        return count;
    }
    // A JavaScript class expression a CommonJS module exports (`module.exports = class {}`, `exports.C = class {}`)
    ClassDecl const* class_expression = symbol->flags & SYMBOL_CLASS ? symbol_find_node(symbol, SYMBOL_DECL_CLASS) : nullptr;
    if (class_expression && class_expression->is_expression && class_expression->commonjs_module) {
        out[count++] = class_expression->commonjs_module;
        return count;
    }
    // A declaration directly in a module that the module exports under another name (`export = C`, `export { C as D }`)
    Symbol const* module = declaring_module(symbol);
    if (module && alias_in_container(me, module, symbol)) out[count++] = module;
    return count;
}

// tsc's getSymbolChain
static bool symbol_chain(ChainCtx* me, Symbol const* symbol, u32 meaning, bool end_of_chain, u32 depth, SymbolChain* out) {
    SymbolChain chain = {0};
    bool have = accessible_chain(me, symbol, meaning, &chain);
    if (depth < 8 && (!have || needs_qualification(me, chain.items[0], chain.len == 1 ? meaning : left_meaning(meaning)))) {
        Symbol const* parents[CONTAINERS_MAX];
        u32 parent_count = containers_of(me, have ? chain.items[0] : symbol, meaning, parents);
        for (u32 i = 0; i < parent_count; i++) {
            SymbolChain parent_chain = {0};
            if (!symbol_chain(me, parents[i], left_meaning(meaning), false, depth + 1, &parent_chain)) continue;
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
    return unicode_is_identifier_start(code, compiler_options()->target >= SCRIPT_TARGET_ES2015);
}

// tsc's canUsePropertyAccess
static bool can_use_property_access(csview name) {
    if (!name.size) return false;
    const char* end = name.buf + name.size;
    if (name.buf[0] == '#') return name.size > 1 && is_identifier_start(name.buf + 1, end);
    return is_identifier_start(name.buf, end);
}

static bool is_quote(char c) {
    return c == '"' || c == '\'';
}

static SourceFile const* file_of_scope(Scope const* scope) {
    return scope ? scope->file : nullptr;
}

// tsc's getNameOfSymbolAsWritten
static csview written_name(Scope const* enclosing, Symbol const* symbol, bool first) {
    Span display = ty_symbol_display_name(symbol);
    csview name = span_valid(display) ? display.sv : symbol->name;
    // An `export default` declaration is `default` except where it was declared
    if (csview_equals(export_name(symbol), "default") &&
        (!first || !span_valid(symbol->name_span) || symbol->name_span.source != file_of_scope(enclosing))
    ) {
        return c_sv("default");
    }
    // A declaration whose name is missing (`enum void {}`: tsc's missing identifier)
    if (!name.size && span_valid(symbol->name_span) && !symbol->name_span.sv.size) return c_sv("(Missing)");
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
            char c[2] = { inner.buf[i], 0 };
            cstr_push(out, c);
        }
        cstr_push(out, quote == '\'' ? "'" : "\"");
    } else {
        cstr_append_sv(out, name);
    }
    cstr_append_lit(out, "]");
}

void symbol_chain_append(cstr* out, Scope const* enclosing, SymbolChain const* chain) {
    symbol_chain_append_entity(out, enclosing, chain, false);
}

// A member only `this.x = v` declares, of a JavaScript constructor function assigned to a variable or property (`ns.F =
// function G() { this.x = v }`): tsc declares it on the function expression's own symbol (G), which has no parent
static bool is_js_this_member_of_assigned_class(Symbol const* container, Symbol const* member, csview* name) {
    if (!member->decl_count || !ty_js_assigned_class_name(container, name)) return false;
    for (u32 i = 0; i < member->decl_count; i++) {
        if (member->decls[i].kind != SYMBOL_DECL_ASSIGNMENT) return false;
        if (member->decls[i].assignment != ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR &&
            member->decls[i].assignment != ASSIGNMENT_DECL_THIS_IN_METHOD
        ) {
            return false;
        }
    }
    return true;
}

void symbol_chain_append_entity(cstr* out, Scope const* enclosing, SymbolChain const* chain, bool entity) {
    isize start = cstr_size(out);
    bool restart = false; // The next name starts the text
    for (u32 i = 0; i < chain->len; i++) {
        Symbol const* symbol = chain->items[i];
        csview class_name;
        if (!entity && i + 1 == chain->len - 1 && chain->items[i + 1]->prototype_literal_member) {
            // (Of an object literal: no name)
            cstr_resize(out, start, 0);
            restart = true;
            continue;
        }
        if (!entity && i + 1 < chain->len && is_js_this_member_of_assigned_class(symbol, chain->items[i + 1], &class_name)) {
            cstr_resize(out, start, 0);
            cstr_append_sv(out, class_name);
            continue;
        }
        csview name = written_name(enclosing, symbol, i == 0);
        // (An entity name has no element accesses: a quoted name that is an identifier is written as one, tsc's
        // symbolToTypeNode of an `o["x"] = v` expando is `typeof o.x`)
        if (entity && i && name.size >= 2 && is_quote(name.buf[0]) && name.buf[name.size - 1] == name.buf[0]) {
            csview inner = csview_slice(name, 1, name.size - 1);
            bool plain = can_use_property_access(inner);
            for (isize k = 0; plain && k < inner.size; k++) plain = inner.buf[k] != '\\' && inner.buf[k] != '-' && inner.buf[k] != ' ';
            if (plain) name = inner;
        }
        if (i == 0 || restart) {
            cstr_append_sv(out, name);
            restart = false;
        } else {
            symbol_chain_append_member(out, name, symbol->flags & SYMBOL_ENUM_MEMBER);
        }
    }
}
