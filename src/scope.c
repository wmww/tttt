#include "symbol_chain.h"
#include "scope.h"
#include "unicode_case.h"
#include "ast_walk.h"
#include "several_types.h"
#include "error.h"
#include "source_file.h"
#include "generic.h"
#include "program.h"
#include "port_statement.h"
#include "function_impl.h"
#include "callable_type.h"
#include "class_decl.h"
#include "interface_decl.h"
#include <math.h>
#include "ty/ty.h"
#include "lib_features.h"
#include "statement.h"
#include "enum_decl.h"
#include "unused.h"

typedef enum MisspelledFallback { FALLBACK_NAMESPACE, FALLBACK_JSX_FACTORY } MisspelledFallback;
static void report_misspelled_or(
    Scope const* me, Span at, csview name, u32 meaning, bool namespace, MisspelledFallback fallback, ErrorCtx* errs
);

// Symbol tables

#define SYMBOL_TABLE_SCAN_MAX 8

// Tables are keyed by cooked names (name_cooked(): `\u0061` is `a`), the entries hold them cooked
u64 symbol_name_hash(csview name) {
    name = name_cooked(name);
    u64 h = 0xcbf29ce484222325ull;
    for (isize i = 0; i < name.size; i++) {
        h ^= (unsigned char)name.buf[i];
        h *= 0x100000001b3ull;
    }
    return h ^ (h >> 32);
}

static inline bool entry_matches(SymbolEntry const* entry, csview name, u64 hash) {
    return entry->hash == (u32)hash && entry->name_len == (u32)name.size && !memcmp(entry->name_buf, name.buf, (size_t)name.size);
}

static Symbol* symbol_table_get_own(SymbolTable const* me, csview name, u64 hash) {
    name = name_cooked(name);
    if (!my index) {
        for (u32 i = 0; i < my count; i++) {
            if (entry_matches(&my entries[i], name, hash)) return my entries[i].symbol;
        }
        return nullptr;
    }
    u32 mask = my index_cap - 1;
    for (u32 pos = (u32)hash & mask; my index[pos]; pos = (pos + 1) & mask) {
        SymbolEntry const* entry = &my entries[my index[pos] - 1];
        if (entry_matches(entry, name, hash)) return entry->symbol;
    }
    return nullptr;
}

Symbol* symbol_table_lookup(SymbolTable const* me, csview name, u64 hash) {
    for (SymbolTable const* i = me; i; i = i->parent) {
        Symbol* ret = symbol_table_get_own(i, name, hash);
        if (ret) return ret;
    }
    return nullptr;
}

static void symbol_table_index_insert(SymbolTable* me, u32 entry_index) {
    u32 mask = my index_cap - 1;
    u32 pos = (u32)my entries[entry_index].hash & mask;
    while (my index[pos]) pos = (pos + 1) & mask;
    my index[pos] = entry_index + 1;
}

// The name must not be in the table itself yet (it may be in a table it overlays)
static void symbol_table_insert(SymbolTable* me, Arena* arena, csview name, u64 hash, Symbol* symbol) {
    name = name_cooked(name);
    if (my count == my cap) {
        u32 cap = my cap ? my cap * 2 : 1; // (Most scopes declare one or two names)
        SymbolEntry* entries = arena_new_n(arena, SymbolEntry, cap);
        if (my count) memcpy(entries, my entries, sizeof(SymbolEntry) * my count);
        my entries = entries;
        my cap = cap;
    }
    my entries[my count++] = symbol_entry_make(name, hash, symbol);
    if (my count > SYMBOL_TABLE_SCAN_MAX && my count * 2 > my index_cap) {
        my index_cap = my index_cap ? my index_cap * 2 : 32;
        my index = arena_new_n(arena, u32, my index_cap);
        for (u32 i = 0; i < my count; i++) symbol_table_index_insert(me, i);
    } else if (my index) {
        symbol_table_index_insert(me, my count - 1);
    }
}

// An overlaid table replaces a symbol of its parent by a clone, see clone_parent_symbol()
static void symbol_table_collect_into(SymbolTable const* me, SymbolTable const* top, SymbolEntry* out, u32* count) {
    if (!me) return;
    symbol_table_collect_into(my parent, top, out, count);
    for (u32 i = 0; i < my count; i++) {
        bool shadowed = false;
        for (SymbolTable const* above = top; above != me && !shadowed; above = above->parent) {
            shadowed = symbol_table_get_own(above, symbol_entry_name(&my entries[i]), my entries[i].hash);
        }
        if (!shadowed) out[(*count)++] = my entries[i];
    }
}

SymbolEntry* symbol_table_collect(SymbolTable const* me, u32* count) {
    *count = 0;
    u32 total = 0;
    for (SymbolTable const* i = me; i; i = i->parent) total += i->count;
    if (!total) return nullptr;
    SymbolEntry* ret = malloc(sizeof(SymbolEntry) * total);
    symbol_table_collect_into(me, me, ret, count);
    return ret;
}

// Binder

Binder* binder_new(ErrorCtx* errs) {
    Binder* me = c_new(Binder, { .errs = errs });
    my global_scope = arena_new(&my arena, Scope);
    *my global_scope = (Scope){ .kind = SCOPE_GLOBAL, .binder = me };
    return me;
}

thread_local ResolveShard* resolve_shard;
// Symbols and declarations this thread made, for scope_pending_mark()
static thread_local u64 binder_made;

// A new last element of a malloc'd list
static void* list_push(void** data, u32* count, u32* cap, size_t size) {
    if (*count == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        *data = realloc(*data, size * *cap);
        panic_if(!*data, "out of memory");
    }
    return (char*)*data + size * (*count)++;
}

static ResolveShard* binder_shard(Binder const* me) {
    return resolve_shard && resolve_shard->binder == me ? resolve_shard : nullptr;
}

Arena* binder_thread_arena(Binder* me, u32 t) {
    if (t > my thread_arena_count) {
        my thread_arenas = realloc(my thread_arenas, sizeof(Arena) * 2 * t);
        panic_if(!my thread_arenas, "out of memory");
        for (u32 i = 2 * my thread_arena_count; i < 2 * t; i++) my thread_arenas[i] = (Arena){0};
        my thread_arena_count = t;
    }
    return &my thread_arenas[2 * (t - 1)];
}

Arena* binder_thread_scope_arena(Binder* me, u32 t) {
    return binder_thread_arena(me, t) + 1;
}

ErrorCtx* binder_discarded_errs(Binder* me) {
    if (resolve_shard && resolve_shard->binder == me) return &resolve_shard->discarded;
    if (!my discarded_errs) my discarded_errs = c_new(ErrorCtx, error_ctx_init());
    return my discarded_errs;
}

void binder_destroy(Binder* me) {
    if (!me) return;
    free(my symbols);
    free(my pattern_modules);
    free(my augmentations);
    free(my global_augmentations);
    free(my pending_not_found);
    free(my pending_type_as_value);
    free(my pending_export_merges);
    free(my pending_alias_merges);
    if (my discarded_errs) {
        error_ctx_drop(my discarded_errs);
        free(my discarded_errs);
    }
    free(my overlays.keys);
    free(my overlays.values);
    for (u32 i = 0; i < 2 * my thread_arena_count; i++) arena_drop(&my thread_arenas[i]);
    free(my thread_arenas);
    arena_drop(&my arena);
    arena_drop(&my scope_arena);
    free(me);
}

void binder_set_parent(Binder* me, Binder const* parent) {
    check(!my globals.parent);
    my globals.parent = &parent->globals;
    my ambient_modules.parent = &parent->ambient_modules;
    my parent = parent;
}

// Overlays

static u32 symbol_map_pos(SymbolMap const* me, Symbol const* key) {
    u64 h = (u64)(uintptr_t)key * 0x9e3779b97f4a7c15ull;
    u32 pos = (u32)(h >> 32) & (my cap - 1);
    while (my keys[pos] && my keys[pos] != key) pos = (pos + 1) & (my cap - 1);
    return pos;
}

static void symbol_map_set(SymbolMap* me, Symbol const* key, Symbol* value) {
    if ((my count + 1) * 2 > my cap) {
        SymbolMap old = *me;
        my cap = old.cap ? old.cap * 2 : 16;
        my keys = calloc(my cap, sizeof(Symbol const*));
        my values = calloc(my cap, sizeof(Symbol*));
        panic_if(!my keys || !my values, "out of memory");
        for (u32 i = 0; i < old.cap; i++) {
            if (!old.keys[i]) continue;
            u32 pos = symbol_map_pos(me, old.keys[i]);
            my keys[pos] = old.keys[i];
            my values[pos] = old.values[i];
        }
        free(old.keys);
        free(old.values);
    }
    u32 pos = symbol_map_pos(me, key);
    if (!my keys[pos]) my count++;
    my keys[pos] = key;
    my values[pos] = value;
}

Symbol const* binder_redirect_slow(Binder const* me, Symbol const* symbol) {
    u32 pos = symbol_map_pos(&my overlays, symbol);
    return my overlays.keys[pos] ? my overlays.values[pos] : symbol;
}

Symbol* binder_new_symbol(Binder* me, csview name, u32 flags) {
    if (my frozen) log_error("Symbol '", name, "' made while typechecking: the checkers share the binder");
    Symbol* ret = arena_new(binder_arena(me), Symbol);
    binder_made++;
    ret->name = name;
    ret->flags = flags;
    ret->binder = me;
    if (flags & SYMBOL_ALIAS) ret->alias = arena_new(binder_arena(me), SymbolAlias);
    ResolveShard* shard = resolve_shard && resolve_shard->binder == me ? resolve_shard : nullptr;
    if (shard) {
        if (shard->symbol_count == shard->symbol_cap) {
            shard->symbol_cap = shard->symbol_cap ? shard->symbol_cap * 2 : 64;
            shard->symbols = realloc(shard->symbols, sizeof(Symbol*) * shard->symbol_cap);
            panic_if(!shard->symbols, "out of memory");
        }
        shard->symbols[shard->symbol_count++] = ret;
        return ret;
    }
    if (my symbol_count == my symbol_cap) {
        my symbol_cap = my symbol_cap ? my symbol_cap * 2 : 64;
        my symbols = realloc(my symbols, sizeof(Symbol*) * my symbol_cap);
    }
    my symbols[my symbol_count++] = ret;
    return ret;
}

SymbolRare const symbol_rare_none;

SymbolRare* symbol_rare_new(Symbol* me) {
    if (my binder->frozen) log_error("Symbol '", my name, "' given rare fields while typechecking: the checkers share it");
    return my rare = arena_new(binder_arena(my binder), SymbolRare);
}

// After a struct copy of another symbol: a copy of its rare fields of its own
static void symbol_own_rare(Symbol* me) {
    if (!my rare) return;
    SymbolRare* rare = arena_new(binder_arena(my binder), SymbolRare);
    *rare = *my rare;
    my rare = rare;
}

// A SymbolDecl.js: always for an assignment (whose flow container the resolve walk may set), else only with a type
static SymbolDeclJs* symbol_decl_js(Binder* binder, struct TypeNode const* jsdoc_type, bool assignment) {
    if (!assignment && !jsdoc_type) return nullptr;
    SymbolDeclJs* ret = arena_new(binder_arena(binder), SymbolDeclJs);
    ret->jsdoc_type = jsdoc_type;
    return ret;
}

static void symbol_ensure_exports(Symbol* me) {
    if (!my exports) my exports = arena_new(binder_arena(my binder), SymbolTable);
}

Symbol* binder_new_module_symbol(Binder* me, SourceFile* file) {
    Symbol* ret = binder_new_symbol(me, c_sv(""), SYMBOL_VALUE_MODULE);
    ret->is_module = true;
    ret->module = arena_new(binder_arena(me), SymbolModule);
    ret->module->file = file;
    symbol_ensure_exports(ret);
    return ret;
}

void binder_declare_global_this(Binder* me) {
    csview name = c_sv("globalThis");
    Symbol* symbol = binder_new_symbol(me, name, SYMBOL_VALUE_MODULE);
    symbol->is_global_this = true;
    symbol->name_span = sv_make_span(name);
    my global_this = symbol;
    symbol->exports = &my globals; // Its parent table included, when there is one
    symbol_table_insert(&my globals, binder_arena(me), name, symbol_name_hash(name), symbol);
}

static void symbol_add_decl(Symbol* me, SymbolDecl decl);

void binder_add_module_declaration(Symbol* module, NamespaceDecl const* decl) {
    symbol_add_decl(module, (SymbolDecl){
        .kind = SYMBOL_DECL_NAMESPACE, .flags = SYMBOL_VALUE_MODULE, .node = decl, .name = decl->name, .full = decl->full_span,
    });
}

static void add_pattern_module(Binder* me, Symbol* module) {
    if (my pattern_module_count == my pattern_module_cap) {
        my pattern_module_cap = my pattern_module_cap ? my pattern_module_cap * 2 : 4;
        my pattern_modules = realloc(my pattern_modules, sizeof(Symbol*) * my pattern_module_cap);
    }
    my pattern_modules[my pattern_module_count++] = module;
}

static bool attributes_identical(TypeNode const* a, TypeNode const* b);

Symbol* binder_ambient_module(Binder* me, csview name, Span name_span, TypeNode const* attributes) {
    bool pattern = memchr(name.buf, '*', (size_t)name.size);
    // `with {}` is the attributes of a pattern without any (tsgo merges patterns of identical attribute types)
    if (attributes && !vec_interface_member_size(&attributes->interface.members)) attributes = nullptr;
    if (pattern && attributes) {
        // A symbol of its own per pattern and attributes, which no name finds (tsgo's binder names it after its node)
        for (u32 i = 0; i < my pattern_module_count; i++) {
            Symbol* existing = my pattern_modules[i];
            if (csview_equals_sv(csview_slice(existing->name, 1, existing->name.size - 1), name) &&
                existing->module->attributes && attributes_identical(existing->module->attributes, attributes)
            ) {
                return existing;
            }
        }
        Symbol* ret = binder_new_symbol(me, span_sv(name_span), SYMBOL_VALUE_MODULE);
        ret->is_module = true;
        ret->module = arena_new(binder_arena(me), SymbolModule);
        ret->module->attributes = attributes;
        ret->name_span = name_span;
        symbol_ensure_exports(ret);
        add_pattern_module(me, ret);
        my attributed_patterns = true;
        return ret;
    }
    u64 hash = symbol_name_hash(name);
    // TODO: one of the shared lib program would want an overlay like the globals (no lib declares any)
    Symbol* ret = symbol_table_lookup(&my ambient_modules, name, hash);
    if (ret) return ret;
    ret = binder_new_symbol(me, span_sv(name_span), SYMBOL_VALUE_MODULE);
    ret->is_module = true;
    ret->module = arena_new(binder_arena(me), SymbolModule);
    ret->module->attributes = attributes; // (Printed, TS1550: a name without `*` matches no attributes)
    ret->name_span = name_span;
    symbol_ensure_exports(ret);
    symbol_table_insert(&my ambient_modules, binder_arena(me), name, hash, ret);
    if (pattern) add_pattern_module(me, ret);
    return ret;
}

// A JSDoc `@typedef`, `@callback` or `@enum` (or a namespace of a dotted typedef name): tsc's binder declares them once
// the rest of the file is bound (delayedBindJSDocTypedefTag)
static bool is_delayed_jsdoc_decl(SymbolDecl const* decl) {
    if (decl->kind == SYMBOL_DECL_TYPE_ALIAS) return ((TypeDecl const*)decl->node)->jsdoc;
    if (decl->kind == SYMBOL_DECL_NAMESPACE) return ((NamespaceDecl const*)decl->node)->jsdoc;
    return false;
}

static void symbol_add_decl(Symbol* me, SymbolDecl decl) {
    binder_made++;
    if (my decl_count == my decl_cap) {
        u32 cap = my decl_cap ? my decl_cap * 2 : 1; // Most symbols have one declaration
        SymbolDecl* decls = arena_new_n(binder_arena(my binder), SymbolDecl, cap);
        if (my decl_count) memcpy(decls, my decls, sizeof(SymbolDecl) * my decl_count);
        my decls = decls;
        my decl_cap = cap;
    }
    // After the file's declarations of other kinds that tttt declares later than its JSDoc type aliases (expandos)
    u32 at = my decl_count;
    if (at && span_source(decl.full) && is_delayed_jsdoc_decl(&my decls[at - 1]) && !is_delayed_jsdoc_decl(&decl)) {
        while (at && span_source(my decls[at - 1].full) == span_source(decl.full) && is_delayed_jsdoc_decl(&my decls[at - 1])) at--;
        memmove(&my decls[at + 1], &my decls[at], sizeof(SymbolDecl) * (my decl_count - at));
    }
    my decls[at] = decl;
    my decl_count++;
}

// Stands for something that couldn't be resolved: has every meaning, and everything about it is any
static Symbol* binder_new_placeholder(Binder* me, Span name, Span full) {
    Symbol* ret = binder_new_symbol(
        me, span_sv(name), SYMBOL_FUNCTION_SCOPED_VARIABLE | SYMBOL_TYPE_ALIAS | SYMBOL_NAMESPACE_MODULE
    );
    ret->is_placeholder = true;
    ret->name_span = name;
    symbol_add_decl(ret, (SymbolDecl){ .name = name, .full = full });
    return ret;
}

SymbolDecl const* symbol_find_decl(Symbol const* me, SymbolDeclKind kind) {
    if (!me) return nullptr;
    for (u32 i = 0; i < my decl_count; i++) {
        if (my decls[i].kind == kind) return &my decls[i];
    }
    return nullptr;
}

VecSpan symbol_declaration_spans(Symbol const* me) {
    VecSpan ret = vec_span_init();
    if (!me) return ret;
    // A global's declarations in `declare global` blocks come after those of scripts, whatever the file order (tsc's
    // initializeTypeChecker merges every script's locals into the globals before the global augmentations; binder_link()
    // does too, but a program sharing its libs has merged the libs' augmentations already)
    u32 last_script = 0;
    bool deferred = false;
    for (u32 i = 0; i < my decl_count; i++) {
        SourceFile const* file = span_source(my decls[i].full);
        bool in_augmentation = my decls[i].block && my decls[i].block->kind == SCOPE_GLOBAL_AUGMENTATION;
        if (in_augmentation) deferred = true;
        else if (deferred && file && !file->is_module) last_script = i;
    }
    if (last_script) {
        SymbolDecl* decls = malloc(sizeof(SymbolDecl) * my decl_count);
        u32 len = 0;
        for (u32 j = 0; j <= last_script; j++) {
            if (!(my decls[j].block && my decls[j].block->kind == SCOPE_GLOBAL_AUGMENTATION)) decls[len++] = my decls[j];
        }
        for (u32 j = 0; j <= last_script; j++) {
            if (my decls[j].block && my decls[j].block->kind == SCOPE_GLOBAL_AUGMENTATION) decls[len++] = my decls[j];
        }
        for (u32 j = last_script + 1; j < my decl_count; j++) decls[len++] = my decls[j];
        Symbol reordered = *me;
        reordered.decls = decls;
        ret = symbol_declaration_spans(&reordered);
        free(decls);
        return ret;
    }
    // In tsc's order: its binder binds the function declarations of a block before its other statements
    // (bindEachFunctionsFirst). Declarations of the same block are grouped (the file, without a block)
    #define same_block(a, b) ((a).block && (b).block ? (a).block == (b).block : span_source((a).full) == span_source((b).full))
    for (u32 i = 0; i < my decl_count; i++) {
        bool seen = false;
        for (u32 j = 0; j < i; j++) seen |= same_block(my decls[j], my decls[i]);
        if (seen) continue;
        for (u32 pass = 0; pass < 2; pass++) {
            isize from = vec_span_size(&ret);
            bool assignments = false, jsdoc = false, jsdoc_namespace = false, jsdoc_alias = false;
            for (u32 j = i; j < my decl_count; j++) {
                if (!same_block(my decls[j], my decls[i]) || !span_valid(my decls[j].full)) continue;
                // (The namespace of a JSDoc `@enum` or `@typedef` on `A.B.C = v` is the declaration the assignment's
                // name made already: one node to tsc, which appendIfUnique adds once)
                if (is_delayed_jsdoc_decl(&my decls[j])) {
                    bool same_node = false;
                    for (u32 k = 0; k < my decl_count && !same_node; k++) {
                        same_node = k != j && !is_delayed_jsdoc_decl(&my decls[k]) && span_source(my decls[k].full) == span_source(my decls[j].full) &&
                            span_start_ptr(my decls[k].full) == span_start_ptr(my decls[j].full);
                    }
                    if (same_node) continue;
                }
                // (tsgo's binder has no bindPotentiallyMissingNamespaces: outside JavaScript an expando assignment
                // declares the member, not its host. PROTOTYPE: JavaScript keeps 6.0's until its rework, todo entry 8)
                if (my decls[j].kind == SYMBOL_DECL_EXPANDO && !span_source(my decls[j].full)->is_js) continue;
                if ((my decls[j].kind == SYMBOL_DECL_FUNCTION) == (pass == 0)) {
                    // (A JavaScript function's `@overload` tags are declarations of their own before it, at their tag
                    // names: tsgo's reparsed signatures)
                    FunctionImpl const* function = my decls[j].kind == SYMBOL_DECL_FUNCTION ? my decls[j].node : nullptr;
                    for (u32 o = 0; function && o < function_impl_rare(function)->jsdoc_overload_count; o++) {
                        Span at = function_impl_rare(function)->jsdoc_overloads[o].callable->function->full_span;
                        if (span_valid(at) && at.size > 1) vec_span_push(&ret, span_make_in(at, at.buf + 1, at.buf + at.size));
                    }
                    vec_span_push(&ret, my decls[j].full);
                    assignments |= my decls[j].kind == SYMBOL_DECL_ASSIGNMENT || my decls[j].kind == SYMBOL_DECL_EXPANDO;
                    jsdoc |= span_is_jsdoc_tag(my decls[j].full) || is_delayed_jsdoc_decl(&my decls[j]);
                    jsdoc_namespace |= my decls[j].kind == SYMBOL_DECL_NAMESPACE && is_delayed_jsdoc_decl(&my decls[j]);
                    jsdoc_alias |= my decls[j].kind == SYMBOL_DECL_TYPE_ALIAS && is_delayed_jsdoc_decl(&my decls[j]);
                }
            }
            // (The namespace of a dotted `@typedef A.b` isn't deferred like a top-level typedef, tsgo's reparsed
            // ModuleDeclaration: in source order with the rest, `export {A}` after the typedefs before it)
            bool by_position = jsdoc_namespace && !jsdoc_alias;
            if (!by_position && (!assignments || jsdoc)) continue;
            // (The rest in source order: an expando assignment is declared after the namespaces it is between by
            // tttt's resolve walk, by tsc's binder where it is)
            for (isize a = from + 1; a < vec_span_size(&ret); a++) {
                Span moved = ret.data[a];
                isize b = a;
                while (b > from && span_source(ret.data[b - 1]) == span_source(moved) && ret.data[b - 1].buf > moved.buf) {
                    ret.data[b] = ret.data[b - 1];
                    b--;
                }
                ret.data[b] = moved;
            }
        }
    }
    #undef same_block
    return ret;
}

// Scopes

ScopeRare const scope_rare_none;

ScopeRare* scope_rare_new(Scope* me) {
    return my rare = arena_new(binder_scope_arena(my binder), ScopeRare);
}

struct FunctionImpl const* scope_containing_function(Scope const* me) {
    for (Scope const* scope = me; scope && scope->kind != SCOPE_FUNCTION; scope = scope->parent) {
        if (scope_rare(scope)->decorated_function) return scope_rare(scope)->decorated_function;
    }
    return my function;
}

const SymbolTable symbol_table_empty = {0};

SymbolTable* scope_locals_mut(Scope* me) {
    if (!my locals) my locals = arena_new(binder_arena(my binder), SymbolTable);
    return my locals;
}

// Over a million on a big program, in the binder arena
static_assert(sizeof(Scope) == 96, "Scope grew");

static_assert(sizeof(SymbolDecl) == 64, "SymbolDecl grew");

Scope* scope_new(Scope* parent, ScopeKind kind) {
    Scope* ret = arena_new(binder_scope_arena(parent->binder), Scope);
    ret->parent = parent;
    ret->kind = kind;
    ret->binder = parent->binder;
    ret->ambient = parent->ambient;
    ret->flow = parent->flow;
    ret->file = parent->file;
    ret->strict = parent->strict;
    ret->class_strict = parent->class_strict;
    ret->generics = parent->generics;
    if (kind == SCOPE_BLOCK) ret->function = parent->function;
    return ret;
}

ScopePendingMark scope_pending_mark(Binder const* binder) {
    ResolveShard const* shard = resolve_shard;
    if (shard && shard->binder == binder) {
        return (ScopePendingMark){ shard->pending_not_found_count, shard->pending_export_merge_count, shard->deferred_count, binder_made };
    }
    return (ScopePendingMark){ binder->pending_not_found_count, binder->pending_export_merge_count, 0, binder_made };
}

bool scope_release(Scope* me, ScopePendingMark pending_mark) {
    ScopePendingMark now = scope_pending_mark(my binder);
    if (now.not_found != pending_mark.not_found || now.export_merges != pending_mark.export_merges ||
        now.deferred != pending_mark.deferred || now.made != pending_mark.made || my locals
    ) {
        return false;
    }
    Arena* arena = binder_scope_arena(my binder);
    bool popped_rare = false;
    if (my rare) {
        // (Its rare part right after it, as when it was given one before anything else was allocated)
        if ((char*)my rare != (char*)me + sizeof(Scope) || !arena_pop_last(arena, my rare, sizeof(ScopeRare))) return false;
        my rare = nullptr;
        popped_rare = true;
    }
    bool popped = arena_pop_last(arena, me, sizeof(Scope));
    if (!popped && popped_rare) panic("a scope's rare part was the latest allocation, not the scope");
#ifdef TTTT_POISON_SCOPES
    if (popped) {
        size_t size = sizeof(Scope) + (popped_rare ? sizeof(ScopeRare) : 0);
        void* filler = arena_alloc(arena, size);
        check(filler == (void*)me);
        memset(filler, 0xA5, size);
    }
#endif
    return popped;
}

void scope_reparent(Scope* me, Scope* parent) {
    my parent = parent;
    my ambient = parent->ambient;
    my flow = parent->flow;
    my file = parent->file;
    my strict = parent->strict;
    my class_strict = parent->class_strict;
    my generics = parent->generics;
    if (my kind == SCOPE_BLOCK) my function = parent->function;
}

bool block_has_use_strict(Block const* block) {
    for_each (i, vec_statement, &block->statements) {
        if (i->tag != STATEMENT_EXPR || i->expr.tag != EXPR_LITERAL || i->expr.literal.tag != VALUE_STRING ||
            expr_is_perenthisized(&i->expr)
        ) {
            return false;
        }
        csview text = span_sv(i->expr.inner_span);
        if (csview_equals(text, "\"use strict\"") || csview_equals(text, "'use strict'")) return true;
    }
    return false;
}

// 2: in a class, 1: in a module (which says why the code is strict), else 0
static int strict_reason(Scope const* me) {
    if (my class_strict) return 2;
    for (Scope const* scope = me; scope; scope = scope->parent) {
        if (scope_rare(scope)->this_ref.class_decl) return 2;
    }
    return my file && my file->is_module ? 1 : 0;
}

static bool is_future_reserved_word(Span name) {
    static char const* const reserved[] = { "implements", "interface", "let", "package", "private", "protected", "public",
        "static", "yield" };
    for (u32 i = 0; i < c_arraylen(reserved); i++) if (csview_equals(span_sv(name), reserved[i])) return true;
    return false;
}

void scope_check_strict_class_name(Scope const* me, Span name) {
    if (my ambient || !span_valid(name) || !span_source(name) || span_source(name)->has_parse_errors) return;
    if (!is_future_reserved_word(name)) {
        scope_check_strict_identifier(me, name, false);
        return;
    }
    error_ctx_error_d(binder_errs(my binder), name, 1213, "Identifier expected. '", name, "' is a reserved word in strict mode. "
        "Class definitions are automatically in strict mode.");
}

void scope_check_strict_identifier(Scope const* me, Span name, bool in_type) {
    if (my ambient || !span_valid(name) || !span_source(name) || span_source(name)->has_parse_errors) return;
    bool is_reserved = is_future_reserved_word(name);
    if (!my strict || !is_reserved) {
        // (tsc's checkContextualIdentifier: `await` and `yield` where they are operators)
        // (Not in types: the parser has no await or yield context there)
        bool await = !in_type && csview_equals(span_sv(name), "await"), yield = !in_type && csview_equals(span_sv(name), "yield");
        // The await/yield context: of the nearest function, a static block (await), nothing past a class boundary
        // (member initializers are an implicit function)
        // (A function expression's name is in its function's context as far as that is async or a generator, and in
        // the outer one otherwise)
        bool await_context = false, yield_context = false, top_level = true;
        for (Scope const* scope = me; scope; scope = scope->parent) {
            if (scope_rare(scope)->expression_name_of) {
                await_context |= scope_rare(scope)->expression_name_of->is_async;
                yield_context |= scope_rare(scope)->expression_name_of->is_generator;
                continue;
            }
            if (scope->kind == SCOPE_FUNCTION && scope->function) {
                await_context |= scope->function->is_async;
                yield_context |= scope->function->is_generator;
            } else if (scope->kind == SCOPE_STATIC_BLOCK) {
                await_context = true;
            } else if (!scope_rare(scope)->this_ref.class_decl) {
                continue;
            }
            top_level = false;
            break;
        }
        if (await && my file && my file->is_module && top_level && !await_context) {
            error_ctx_error_d(binder_errs(my binder), name, 1262, "Identifier expected. '", name, "' is a reserved word at the top-level of a module.");
        } else if ((await && await_context) || (yield && yield_context)) {
            error_ctx_error_d(binder_errs(my binder), name, 1359, "Identifier expected. '", name, "' is a reserved word that cannot be used here.");
        }
        return;
    }
    switch (strict_reason(me)) {
        case 2:
            error_ctx_error_d(binder_errs(my binder), name, 1213, "Identifier expected. '", name, "' is a reserved word in strict mode. "
                "Class definitions are automatically in strict mode.");
            break;
        case 1:
            error_ctx_error_d(binder_errs(my binder), name, 1214, "Identifier expected. '", name, "' is a reserved word in strict mode. "
                "Modules are automatically in strict mode.");
            break;
        default:
            error_ctx_error_d(binder_errs(my binder), name, 1212, "Identifier expected. '", name, "' is a reserved word in strict mode.");
    }
}

void scope_check_strict_eval_or_arguments(Scope const* me, Span name) {
    if (!my strict || !span_valid(name) || (!csview_equals(span_sv(name), "eval") && !csview_equals(span_sv(name), "arguments"))) return;
    switch (strict_reason(me)) {
        case 2:
            error_ctx_error_d(binder_errs(my binder), name, 1210, "Code contained in a class is evaluated in JavaScript's strict mode which "
                "does not allow this use of '", name, "'. For more information, see "
                "https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Strict_mode.");
            break;
        case 1:
            error_ctx_error_d(binder_errs(my binder), name, 1215, "Invalid use of '", name, "'. Modules are automatically in strict mode.");
            break;
        default:
            error_ctx_error_d(binder_errs(my binder), name, 1100, "Invalid use of '", name, "' in strict mode.");
    }
}

Scope* scope_new_self(Scope* parent, Symbol const* self, bool declared) {
    // (Only unused.c reads self: in an ambient context already, a `declare` scope would change nothing)
    if ((!declared || parent->ambient) && (!unused_enabled() || !self)) return parent;
    Scope* ret = scope_new(parent, SCOPE_BLOCK);
    ret->self = self;
    ret->ambient |= declared;
    return ret;
}

Scope* scope_new_file(Binder* binder, SourceFile* file) {
    Scope* ret = scope_new(binder->global_scope, file->is_module ? SCOPE_MODULE : SCOPE_SCRIPT);
    ret->file = file;
    ret->ambient = file->is_declaration;
    // (tsgo 7's binder has no non-strict mode: its strict mode checks apply everywhere, a script declaration file's
    // `declare var arguments` too, TS1100)
    ret->strict = true;
    if (file->is_module) {
        ret->container = binder_new_module_symbol(binder, file);
        ret->export_context = ret->ambient && !block_has_export_declarations(&file->ast);
    }
    return ret;
}

static OuterGenerics* push_generics(Scope* me, Generic const* generic, Span owner) {
    OuterGenerics* outer = arena_new(binder_arena(my binder), OuterGenerics);
    *outer = (OuterGenerics){ .parent = my generics, .generic = generic, .owner = owner, .owner_class = span_null };
    my generics = outer;
    return outer;
}

Scope* scope_new_generic(Scope* parent, Generic* generic, Span owner, Span owner_class, FlowBuilder* flow, ErrorCtx* errs) {
    if (generic_empty(generic)) return parent;
    Scope* ret = scope_new(parent, SCOPE_BLOCK);
    if (flow) ret->flow = flow;
    push_generics(ret, generic, owner)->owner_class = owner_class;
    generic_add_names(generic, ret);
    generic_lookup_names(generic, ret, errs);
    return ret;
}

// The type parameters of an earlier declaration of `owner` (a class or interface)
static Generic const* decl_generic(SymbolDecl const* decl) {
    if (decl->kind == SYMBOL_DECL_INTERFACE) return &((InterfaceDecl const*)decl->node)->generic;
    if (decl->kind == SYMBOL_DECL_CLASS) return &((ClassDecl const*)decl->node)->generic;
    return nullptr;
}

bool symbol_first_declared_elsewhere(Symbol const* symbol, SourceFile const* file) {
    // (Not counting a builtin type the name merges with: a global `var string;`)
    u32 first = symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_BUILTIN ? 1 : 0;
    return symbol->decl_count > first && span_source(symbol->decls[first].full) != file;
}

// A file with a declaration of a merged class or interface whose type parameters may be an earlier declaration's of
// another file (scope_new_generic_merged() writes to those symbols) resolves after the others
void binder_mark_serial_resolves(Binder* me) {
    for (u32 s = 0; s < my symbol_count; s++) {
        Symbol const* owner = my symbols[s];
        if (!(owner->flags & (SYMBOL_CLASS | SYMBOL_INTERFACE)) || owner->decl_count < 2) continue;
        for (u32 i = 1; i < owner->decl_count; i++) {
            Generic const* generic = decl_generic(&owner->decls[i]);
            if (!generic || generic_empty((Generic*)generic)) continue;
            SourceFile* file = (SourceFile*)span_source(owner->decls[i].full);
            for (u32 j = 0; j < i && file && !file->resolve_serial; j++) {
                Generic const* earlier = decl_generic(&owner->decls[j]);
                if (earlier && !generic_empty((Generic*)earlier) && span_source(owner->decls[j].full) != file) file->resolve_serial = true;
            }
        }
    }
}

Scope* scope_new_generic_merged(
    Scope* parent, Generic* generic, Symbol const* owner, void const* node, Span owner_name, ErrorCtx* errs
) {
    if (generic_empty(generic)) return parent;
    Scope* ret = scope_new(parent, SCOPE_BLOCK);
    push_generics(ret, generic, owner_name);
    // Like tsc, which declares them in the members of the merged symbol: a type parameter of the same name as one of
    // an earlier declaration is that one (of any earlier one: `interface I {}` + `interface I<T> {}` + `interface I<T> {}`)
    for_each (i, vec_generic_item, &generic->items) {
        Symbol* shared = nullptr;
        Symbol const* shared_lib = nullptr;
        for (u32 d = 0; owner && d < owner->decl_count && !shared && !shared_lib; d++) {
            if (owner->decls[d].node == node) break;
            Generic const* earlier = decl_generic(&owner->decls[d]);
            for_each (j, vec_generic_item, earlier ? &earlier->items : &(VecGenericItem){0}) {
                if (!j->owned_symbol || !name_equals(span_sv(j->name), span_sv(i->name))) continue;
                // (Not one of the shared lib program: nothing is written to its symbols)
                if (j->owned_symbol->binder == ret->binder) shared = j->owned_symbol;
                else shared_lib = j->owned_symbol;
                break;
            }
        }
        if (!shared || symbol_table_get(scope_locals(ret), span_sv(i->name))) {
            if (shared_lib && !symbol_table_get(scope_locals(ret), span_sv(i->name))) {
                // Merged with a lib declaration (`interface Array<T>`): a symbol of our own that is declared by the
                // lib's type parameter first, like tsc's merged symbol
                Symbol* symbol = binder_new_symbol(ret->binder, span_sv(i->name), SYMBOL_TYPE_PARAMETER);
                symbol->name_span = shared_lib->name_span;
                for (u32 k = 0; k < shared_lib->decl_count; k++) symbol_add_decl(symbol, shared_lib->decls[k]);
                symbol_table_insert(scope_locals_mut(ret), binder_arena(ret->binder), span_sv(i->name), symbol_name_hash(span_sv(i->name)), symbol);
                symbol_add_decl(symbol, (SymbolDecl){
                    .kind = SYMBOL_DECL_TYPE_PARAMETER, .flags = SYMBOL_TYPE_PARAMETER, .node = i, .name = i->name, .full = i->full_span,
                });
                symbol->is_local = true;
                i->owned_symbol = symbol;
                i->symbol_ref = symbol;
                continue;
            }
            scope_init_declaration(ret, &i->owned_symbol, (DeclProps){
                .name = i->name, .full = i->full_span, .kind = SYMBOL_DECL_TYPE_PARAMETER, .node = i,
            });
        } else {
            symbol_table_insert(scope_locals_mut(ret), binder_arena(ret->binder), span_sv(i->name), symbol_name_hash(span_sv(i->name)), shared);
            symbol_add_decl(shared, (SymbolDecl){
                .kind = SYMBOL_DECL_TYPE_PARAMETER, .flags = SYMBOL_TYPE_PARAMETER, .node = i, .name = i->name, .full = i->full_span,
            });
            i->owned_symbol = shared;
        }
        i->symbol_ref = i->owned_symbol;
    }
    generic_lookup_names(generic, ret, errs);
    return ret;
}

bool scope_has_function(Scope const* me) {
    for (Scope const* i = me; i; i = i->parent) {
        if (i->kind == SCOPE_FUNCTION) return true;
    }
    return false;
}

// Declaring

// `display`: the name to say when it isn't the declaration's text (tsc's getDisplayName: `export=`)
// `lib_too`: a lib file's declaration is reported as well (conflicting with a user file's)
static bool symbol_declared_in_block(Symbol const* me, Scope const* block) {
    for (u32 i = 0; i < my decl_count; i++) {
        if (my decls[i].block == block) return true;
    }
    return false;
}

// `merge`: a conflict of tsgo's checker merge (reportMergeSymbolError), whose lookupOrIssueError only finds an identical
// diagnostic: one with related information already (from another merge) leaves this merge its own diagnostic, which
// tsgo's compactAndMergeRelatedInfos joins with it at the end (each merge's first related is "was also declared here")
static Error* report_conflict_create(ErrorCtx* errs, Span at, unsigned code, csview display);

static Error* report_conflict_named(ErrorCtx* errs, Span at, unsigned code, csview display, bool lib_too, bool merge) {
    if (!span_valid(at) || !span_source(at) || (span_source(at)->is_stdlib && !lib_too)) return nullptr;
    // Once per declaration: a later conflict adds to the error that's there (the binder's declareSymbol)
    Error* existing = error_ctx_find(errs, at, code == 2567 || code == 2451 ? code : 2300);
    bool fresh = false;
    if (existing && merge) {
        for_each (i, vec_error, &existing->sub_errors) fresh |= span_valid(i->span);
    }
    if (existing && !fresh) return existing;
    ErrorPhase phase = errs->phase;
    // (A diagnostic of its own at the same span: the checker's collection keeps those)
    if (fresh) errs->phase = ERROR_PHASE_TYPECHECK;
    Error* ret = report_conflict_create(errs, at, code, display);
    errs->phase = phase;
    return ret;
}

static Error* report_conflict_create(ErrorCtx* errs, Span at, unsigned code, csview display) {
    // (A missing name is tsc's "__missing", shown as "(Missing)")
    csview name = display.size ? display : at.size ? span_sv(at) : c_sv("(Missing)");
    Error* (*report)(ErrorCtx*, Span, unsigned, cstr) = error_ctx_error_s;
    switch (code) {
        case 2567:
            return report(errs, at, 2567, cstr_d("Enum declarations can only merge with namespace or other enum declarations."));
        case 2451:
            return report(errs, at, 2451, cstr_d("Cannot redeclare block-scoped variable '", name, "'."));
        default:
            return report(errs, at, 2300, cstr_d("Duplicate identifier '", name, "'."));
    }
}

static Error* report_conflict(ErrorCtx* errs, Span at, unsigned code) {
    return report_conflict_named(errs, at, code, (csview){0}, false, false);
}

// tsc's addDuplicateDeclarationError: the checker's merges (across files) point each error at the other side's
// declarations, the binder's (within a file) don't
void add_duplicate_related(Error* error, Span at, Span other) {
    add_duplicate_related_named(error, at, other, span_sv(at));
}

void add_duplicate_related_named(Error* error, Span at, Span other, csview name) {
    if (!error || span_eq(at, other) || !span_valid(other) || !span_source(other)) return;
    u32 count = 0;
    for_each (i, vec_error, &error->sub_errors) {
        if (!span_valid(i->span)) continue;
        if (span_eq(i->span, other)) return;
        count++;
    }
    // (A missing name is tsc's "__missing", shown as "(Missing)")
    if (!name.size) name = c_sv("(Missing)");
    if (count >= 5) return;
    if (count) {
        error_add_related_d(error, other, 6204, "and here.");
    } else {
        error_add_related_d(error, other, 6203, "'", name, "' was also declared here.");
    }
}

static Span first_token(Span full);

// tsc's getNameOfDeclaration(): `export default x` is x, `export default <expr>` the statement, a nameless declaration
// its first token
static Span default_error_span(SymbolDecl const* decl) {
    return span_valid(decl->name) ? decl->name : first_token(decl->full);
}

// tsc's declareSymbol for a second default export: TS2528 on every declaration, related to the new one ("Another export
// default is here" on the first, "and here" on the others) and from the new one to each ("The first export default is
// here")
static void report_multiple_defaults(ErrorCtx* errs, Symbol const* existing, Span name) {
    for (u32 i = 0; i < existing->decl_count; i++) {
        Span at = default_error_span(&existing->decls[i]);
        if (!span_valid(at) || !span_source(at) || span_source(at)->is_stdlib) continue;
        Error* error = error_ctx_error_d(errs, at, 2528, "A module cannot have multiple default exports.");
        if (!span_valid(name)) continue;
        if (i == 0) error_add_related_d(error, name, 2753, "Another export default is here.");
        else error_add_related_d(error, name, 6204, "and here.");
    }
    if (!span_valid(name) || !span_source(name) || span_source(name)->is_stdlib) return;
    Error* error = error_ctx_error_d(errs, name, 2528, "A module cannot have multiple default exports.");
    for (u32 i = 0; i < existing->decl_count; i++) {
        Span at = default_error_span(&existing->decls[i]);
        if (span_valid(at)) error_add_related_d(error, at, 2752, "The first export default is here.");
    }
}

static void report_conflicts(
    ErrorCtx* errs, Symbol const* existing, Span name, u32 includes, bool related, csview display
) {
    if (existing->is_global_this) {
        // tsc's mergeSymbol is silent about the built-in `globalThis`: what a script declares by that name is
        // initializeTypeChecker's error (a module's `declare global` declarations aren't)
        if (span_valid(name) && span_source(name) && !span_source(name)->is_stdlib && !span_source(name)->is_module) {
            error_ctx_error_d(errs, name, 2397, "Declaration name conflicts with built-in global identifier 'globalThis'.");
        }
        return;
    }
    bool same_file = existing->decl_count && span_source(existing->decls[0].name) == span_source(name);
    if (!same_file && existing->flags & SYMBOL_NAMESPACE_MODULE && !existing->is_global_this) {
        // Across files symbols are merged by the checker in tsc, whose mergeSymbol says this into a namespace
        if (span_valid(name) && span_source(name) && !span_source(name)->is_stdlib) {
            error_ctx_error_d(errs, name, 2649, "Cannot augment module '", existing->name,
                "' with value exports because it resolves to a non-module entity.");
        }
        return;
    }
    unsigned code = 2300;
    if ((existing->flags | includes) & SYMBOL_ENUM) {
        code = 2567;
    } else if (existing->flags & SYMBOL_BLOCK_SCOPED_VARIABLE) {
        code = 2451;
    } else if ((!same_file || related) && includes & SYMBOL_BLOCK_SCOPED_VARIABLE) {
        // Across files (or for related, where tsc's checker merges) symbols are merged by the checker in tsc, which looks
        // at both sides (reportMergeSymbolError)
        code = 2451;
    }
    related |= !same_file;
    // (tsc's mergeSymbol: a plain JavaScript file's side of a conflict across files isn't reported, the binder's
    // conflicts inside one file are)
    for (u32 i = 0; i < existing->decl_count; i++) {
        Span at = existing->decls[i].name;
        if (!same_file && span_source(at) && span_source(at)->plain_js) continue;
        // (tsc binds a file's JSDoc typedefs after everything else in it, delayedBindJSDocTypedefTag: a conflict
        // between other declarations doesn't count the typedef among them yet)
        SymbolDecl const* other = &existing->decls[i];
        if (same_file && !(includes & SYMBOL_TYPE_ALIAS) && other->kind == SYMBOL_DECL_TYPE_ALIAS && other->node &&
            ((TypeDecl const*)other->node)->jsdoc
        ) {
            continue;
        }
        // (tsc reports both sides of a conflict, a lib file's too when lib files are checked: the harness lists those
        // without a position)
        bool lib_too = span_source(name) && !span_source(name)->is_stdlib && lib_files_checked();
        Error* error = report_conflict_named(errs, at, code, display, lib_too, related);
        if (related) add_duplicate_related(error, at, name);
    }
    if (!same_file && span_source(name) && span_source(name)->plain_js) return;
    Error* error = report_conflict_named(errs, name, code, display, false, related);
    for (u32 i = 0; related && i < existing->decl_count; i++) {
        add_duplicate_related(error, name, existing->decls[i].name);
    }
}

// A program that adds to a symbol of the lib program it shares (an interface, a namespace like Intl) gets a clone to
// add to: nothing is written to the original. What's inside the lib AST keeps pointing at the original, the checker
// gets from there to the clone through binder_redirect()
static Symbol* clone_parent_symbol(Binder* binder, Symbol const* original) {
    Symbol* ret = binder_new_symbol(binder, original->name, original->flags);
    Binder* owner = ret->binder;
    *ret = *original;
    ret->binder = owner;
    symbol_own_rare(ret);
    if (original->decl_count) {
        ret->decls = arena_new_n(binder_arena(binder), SymbolDecl, original->decl_cap);
        memcpy(ret->decls, original->decls, sizeof(SymbolDecl) * original->decl_count);
    }
    if (original->exports) {
        ret->exports = arena_new(binder_arena(binder), SymbolTable);
        ret->exports->parent = original->exports;
    }
    symbol_map_set(&binder->overlays, original, ret);
    return ret;
}

static void scope_declare_default_local(Scope* me, Symbol* symbol, Span name, u32 excludes);

u32 symbol_excludes(u32 includes) {
    u32 ret = 0;
    if (includes & SYMBOL_BLOCK_SCOPED_VARIABLE) ret |= SYMBOL_BLOCK_SCOPED_VARIABLE_EXCLUDES;
    if (includes & SYMBOL_FUNCTION_SCOPED_VARIABLE) ret |= SYMBOL_FUNCTION_SCOPED_VARIABLE_EXCLUDES;
    if (includes & SYMBOL_PROPERTY) ret |= SYMBOL_PROPERTY_EXCLUDES;
    if (includes & SYMBOL_ENUM_MEMBER) ret |= SYMBOL_ENUM_MEMBER_EXCLUDES;
    if (includes & SYMBOL_FUNCTION) ret |= SYMBOL_FUNCTION_EXCLUDES;
    if (includes & SYMBOL_CLASS) ret |= SYMBOL_CLASS_EXCLUDES;
    if (includes & SYMBOL_INTERFACE) ret |= SYMBOL_INTERFACE_EXCLUDES;
    if (includes & SYMBOL_REGULAR_ENUM) ret |= SYMBOL_REGULAR_ENUM_EXCLUDES;
    if (includes & SYMBOL_CONST_ENUM) ret |= SYMBOL_CONST_ENUM_EXCLUDES;
    if (includes & SYMBOL_VALUE_MODULE) ret |= SYMBOL_VALUE_MODULE_EXCLUDES;
    if (includes & SYMBOL_METHOD) ret |= SYMBOL_METHOD_EXCLUDES;
    if (includes & SYMBOL_GET_ACCESSOR) ret |= SYMBOL_GET_ACCESSOR_EXCLUDES;
    if (includes & SYMBOL_SET_ACCESSOR) ret |= SYMBOL_SET_ACCESSOR_EXCLUDES;
    if (includes & SYMBOL_TYPE_PARAMETER) ret |= SYMBOL_TYPE_PARAMETER_EXCLUDES;
    if (includes & SYMBOL_TYPE_ALIAS) ret |= SYMBOL_TYPE_ALIAS_EXCLUDES;
    if (includes & SYMBOL_ALIAS) ret |= SYMBOL_ALIAS_EXCLUDES;
    return ret;
}

// `number`, `any`... (program_add_builtins())
bool symbol_is_builtin_type(Symbol const* symbol) {
    return symbol && symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_BUILTIN && symbol->flags & SYMBOL_TYPE_ALIAS;
}

bool type_name_is_reserved(csview name) {
    static char const* const reserved[] = {
        "any", "unknown", "never", "number", "bigint", "boolean", "string", "symbol", "void", "object", "undefined",
    };
    for (u32 i = 0; i < c_arraylen(reserved); i++) {
        if (csview_equals(name, reserved[i])) return true;
    }
    return false;
}

// tsc's checkTypeNameIsReserved: the predefined type keywords can't name a type
static void check_type_name_is_reserved(ErrorCtx* errs, Span name, SymbolDeclKind kind) {
    char const* what;
    unsigned code;
    switch (kind) {
        case SYMBOL_DECL_CLASS: what = "Class"; code = 2414; break;
        case SYMBOL_DECL_INTERFACE: what = "Interface"; code = 2427; break;
        case SYMBOL_DECL_ENUM: what = "Enum"; code = 2431; break;
        case SYMBOL_DECL_TYPE_ALIAS: what = "Type alias"; code = 2457; break;
        case SYMBOL_DECL_TYPE_PARAMETER: what = "Type parameter"; code = 2368; break;
        default: return;
    }
    if (!span_valid(name) || !span_source(name) || span_source(name)->is_stdlib || !type_name_is_reserved(span_sv(name))) return;
    error_ctx_error_d(errs, name, code, what, " name cannot be '", name, "'.");
}

// tsc's checkCollisionWithRequireExportsInGeneratedCode: a declaration at the top level of a module that is emitted as
// CommonJS, AMD, UMD or System can't be named `require` or `exports`; nor `Object` (but a class) when emitted as
// CommonJS (tsgo 7.1's checkCollisionWithGlobalObjectInGeneratedCode: the emit's `Object.defineProperty(exports, ...)`)
static void check_require_exports_collision(Scope* me, Span name, bool is_class) {
    if (my kind != SCOPE_MODULE || my ambient || !my file || my file->unchecked || !span_valid(name)) return;
    bool object = !is_class && csview_equals(span_sv(name), "Object");
    if (!object && !csview_equals(span_sv(name), "require") && !csview_equals(span_sv(name), "exports")) return;
    // (tsc's errorSkippedOn("noEmit"))
    if (compiler_options()->no_emit) return;
    ModuleKind format = source_file_emit_module_kind(my file);
    if (object ? format != MODULE_KIND_COMMONJS : format >= MODULE_KIND_ES2015) return;
    error_ctx_error_d(
        binder_errs(my binder), name, 2441, "Duplicate identifier '", name, "'. Compiler reserves name '", name,
        "' in top level scope of a module."
    );
}

// tsc's declareSymbol(). *merged is whether the symbol existed already. default_export: a declaration of the `default`
// export of a module, which conflicts with the others as TS2528
static bool is_umd_global(Symbol const* symbol) {
    return symbol->flags & SYMBOL_ALIAS && symbol->alias && symbol->alias->item && symbol->alias->item->tag == PORT_EXPORT_AS_NAMESPACE;
}

// tsc's initializeTypeChecker makes a UMD global (`export as namespace N`) a global unless a script declared one, then
// merges `declare global` augmentations into it: mergeSymbol resolves the alias, a module (or so we assume of what an
// `export =` names: the binder can't resolve it), which conflicts with a variable
static bool umd_global_conflict(Scope* scope, Symbol const* existing, u32 includes, u32 excludes) {
    if (scope->kind == SCOPE_GLOBAL_AUGMENTATION) return is_umd_global(existing) && excludes & SYMBOL_VALUE_MODULE;
    if (scope->kind != SCOPE_MODULE || !(includes & SYMBOL_ALIAS) || !existing->decl_count) return false;
    if (!(existing->flags & (SYMBOL_FUNCTION_SCOPED_VARIABLE | SYMBOL_BLOCK_SCOPED_VARIABLE))) return false;
    // (All of it from augmentations: a script's declaration wins silently)
    for (u32 i = 0; i < existing->decl_count; i++) {
        SourceFile const* file = span_source(existing->decls[i].name);
        if (!file || !file->is_module) return false;
    }
    return true;
}

// tsc's SymbolFlags.Assignment: declared by an assignment (`f.x = 1`, `this.x = 1`)
static bool symbol_has_assignment_decl(Symbol const* me) {
    for (u32 i = 0; i < my decl_count; i++) {
        if (my decls[i].kind == SYMBOL_DECL_ASSIGNMENT) return true;
    }
    return false;
}

static Symbol* declare_symbol(
    Scope* scope, SymbolTable* table, csview name, Span name_span, u32 includes, u32 excludes, bool default_export,
    bool* merged
) {
    Binder* binder = scope->binder;
    *merged = false;
    // A class declaration without a name (`class {}` outside `export default`) is a symbol of its own: tsc's
    // getDeclarationName is undefined for it, where a missing identifier's name ("") merges
    if (!name.buf && !default_export && includes & SYMBOL_CLASS) return binder_new_symbol(binder, name, includes);
    u64 hash = symbol_name_hash(name);
    Symbol* existing = symbol_table_get_own(table, name, hash);
    // `class number {}` (TS2414): the keyword wins, the declaration is a symbol of its own
    Symbol const* builtin = existing ? existing : table->parent ? symbol_table_lookup(table->parent, name, hash) : nullptr;
    if (symbol_is_builtin_type(builtin) && builtin->flags & excludes) return binder_new_symbol(binder, name, includes);
    if (!existing && table->parent) {
        Symbol* inherited = symbol_table_lookup(table->parent, name, hash);
        if (inherited) {
            if (inherited->flags & excludes) {
                report_conflicts(binder_errs(scope->binder), inherited, name_span, includes, false, (csview){0});
                return binder_new_symbol(binder, name, includes);
            }
            existing = clone_parent_symbol(binder, inherited);
            symbol_table_insert(table, binder_arena(binder), existing->name, hash, existing);
        }
    }
    // A function's type parameters and the types its body declares share a declaration space (tsc: the function's
    // locals); tttt keeps the type parameters in the scope around the function's
    if (!existing && scope->kind == SCOPE_FUNCTION && (excludes & SYMBOL_TYPE_PARAMETER) && scope->parent &&
        scope->parent->generics && scope->parent->generics->generic && table == scope->locals
    ) {
        Symbol* param = symbol_table_get_own(scope_locals(scope->parent), name, hash);
        if (param && (param->flags & SYMBOL_TYPE_PARAMETER)) report_conflicts(binder_errs(binder), param, name_span, includes, false, (csview){0});
    }
    if (existing && table == &binder->globals && umd_global_conflict(scope, existing, includes, excludes)) {
        report_conflicts(binder_errs(scope->binder), existing, name_span, includes, true, (csview){0});
        Symbol* ret = binder_new_symbol(binder, name, includes);
        // (tsc's mergeSymbol answers the augmentation's symbol, which becomes the global)
        if (scope->kind == SCOPE_GLOBAL_AUGMENTATION) {
            for (u32 i = 0; i < table->count; i++) {
                if (table->entries[i].symbol == existing) table->entries[i].symbol = ret;
            }
        }
        return ret;
    }
    if (existing) {
        // A global namespace JavaScript expandos of another file made merges with anything (tsc's mergeSymbol: the
        // Assignment flag)
        bool expando_only = existing->decl_count > 0;
        for (u32 i = 0; i < existing->decl_count && expando_only; i++) expando_only = existing->decls[i].kind == SYMBOL_DECL_EXPANDO;
        // (A variable merges with what assignments declared: tsc's declareSymbol, `includes & Variable && flags &
        // Assignment`, `f.x = 1` in one file and `namespace f { export var x }` in another)
        if (existing->flags & excludes && !(expando_only && table == &binder->globals) &&
            !((includes & SYMBOL_VARIABLE) && symbol_has_assignment_decl(existing))
        ) {
            // (Across files it's tsc's checker that merges, whose mergeSymbol reports a duplicate identifier: two
            // ambient `declare module '*.css' { export default classes }`)
            bool same_file = existing->decl_count && span_source(default_error_span(&existing->decls[0])) == span_source(name_span);
            if (default_export && existing->decl_count && same_file) report_multiple_defaults(binder_errs(scope->binder), existing, name_span);
            else report_conflicts(binder_errs(scope->binder), existing, name_span, includes, false,
                csview_equals(name, "export=") ? name : (csview){0});
            return binder_new_symbol(binder, name, includes);
        }
        existing->decl_flags_before = existing->flags;
        existing->flags |= includes;
        if (includes & SYMBOL_ALIAS && !existing->alias) existing->alias = arena_new(binder_arena(binder), SymbolAlias);
        *merged = true;
        return existing;
    }
    Symbol* ret = binder_new_symbol(binder, name, includes);
    symbol_table_insert(table, binder_arena(binder), name, hash, ret);
    return ret;
}

// Where a declaration made in this scope goes
static SymbolTable* scope_declaration_table(Scope* me, bool exported) {
    switch (my kind) {
        case SCOPE_GLOBAL:
        case SCOPE_SCRIPT:
        case SCOPE_GLOBAL_AUGMENTATION:
            return &my binder->globals;

        case SCOPE_MODULE:
        case SCOPE_NAMESPACE:
        case SCOPE_ENUM:
            if ((exported || my export_context) && my container) {
                symbol_ensure_exports(my container);
                return my container->exports;
            }
            break;

        case SCOPE_BLOCK:
        case SCOPE_FUNCTION:
        case SCOPE_STATIC_BLOCK:
            break;
    }
    return scope_locals_mut(me);
}

static void symbol_flags_of_props(DeclProps const* props, u32* includes, u32* excludes) {
    switch (props->kind) {
        case SYMBOL_DECL_NAMESPACE:
            *includes = props->is_instantiated ? SYMBOL_VALUE_MODULE : SYMBOL_NAMESPACE_MODULE;
            *excludes = props->is_instantiated ? SYMBOL_VALUE_MODULE_EXCLUDES : SYMBOL_NAMESPACE_MODULE_EXCLUDES;
            return;
        case SYMBOL_DECL_FUNCTION:
            *includes = SYMBOL_FUNCTION;
            *excludes = SYMBOL_FUNCTION_EXCLUDES;
            return;
        case SYMBOL_DECL_VARIABLE:
        case SYMBOL_DECL_BUILTIN:
        case SYMBOL_DECL_EXPR:
            if (props->kind == SYMBOL_DECL_BUILTIN && !props->function_scoped) {
                *includes = SYMBOL_TYPE_ALIAS;
                *excludes = SYMBOL_TYPE_ALIAS_EXCLUDES;
            } else if (props->is_parameter) {
                *includes = SYMBOL_FUNCTION_SCOPED_VARIABLE;
                *excludes = SYMBOL_PARAMETER_EXCLUDES;
            } else {
                *includes = props->function_scoped ? SYMBOL_FUNCTION_SCOPED_VARIABLE : SYMBOL_BLOCK_SCOPED_VARIABLE;
                *excludes = props->function_scoped ?
                    SYMBOL_FUNCTION_SCOPED_VARIABLE_EXCLUDES : SYMBOL_BLOCK_SCOPED_VARIABLE_EXCLUDES;
            }
            return;
        case SYMBOL_DECL_INTERFACE:
            *includes = SYMBOL_INTERFACE;
            *excludes = SYMBOL_INTERFACE_EXCLUDES;
            return;
        case SYMBOL_DECL_TYPE_ALIAS:
            *includes = SYMBOL_TYPE_ALIAS;
            *excludes = SYMBOL_TYPE_ALIAS_EXCLUDES;
            return;
        case SYMBOL_DECL_TYPE_PARAMETER:
        case SYMBOL_DECL_INFER:
        case SYMBOL_DECL_MAPPED_KEY:
            *includes = SYMBOL_TYPE_PARAMETER;
            *excludes = SYMBOL_TYPE_PARAMETER_EXCLUDES;
            return;
        case SYMBOL_DECL_CLASS:
            *includes = SYMBOL_CLASS;
            *excludes = SYMBOL_CLASS_EXCLUDES;
            return;
        case SYMBOL_DECL_ENUM:
            *includes = props->is_const_enum ? SYMBOL_CONST_ENUM : SYMBOL_REGULAR_ENUM;
            *excludes = props->is_const_enum ? SYMBOL_CONST_ENUM_EXCLUDES : SYMBOL_REGULAR_ENUM_EXCLUDES;
            return;
        case SYMBOL_DECL_ENUM_MEMBER:
            *includes = SYMBOL_ENUM_MEMBER;
            *excludes = SYMBOL_ENUM_MEMBER_EXCLUDES;
            return;
        case SYMBOL_DECL_ALIAS:
        case SYMBOL_DECL_ASSIGNMENT:
        case SYMBOL_DECL_EXPANDO:
        case SYMBOL_DECL_COMMONJS_MODULE:
        case SYMBOL_DECL_COMMONJS_EXPORTS:
        case SYMBOL_DECL_EXPORT_EQUALS_PROPERTY:
        case SYMBOL_DECL_SOURCE_PHASE:
        case SYMBOL_DECL_NONE:
            break;
    }
    panic("declaration kind ", (i32)props->kind, " can't be declared in a scope");
}

// A cross-file merge into an alias of the same table (tsc's mergeSymbol, at checker initialization, resolves the
// alias first and merges into its target, or replaces it if it resolves to nothing): until binder_link() decides it,
// the declaration is a symbol of its own, the file's own like in tsc's binder, see binder_link_alias_merges().
// Returns null when this isn't such a merge
typedef struct PendingAliasMerge {
    SymbolTable* table;
    Symbol* source;
    SourceFile const* file;
} PendingAliasMerge;

static Symbol* declare_pending_alias_merge(
    Scope* scope, SymbolTable* table, csview name, Span name_span, u32 includes, u32 excludes, SymbolDeclKind kind,
    bool* merged, bool* pending
) {
    Binder* binder = scope->binder;
    *merged = false;
    if (binder->alias_merges_done || resolve_shard || (includes & SYMBOL_ALIAS)) return nullptr;
    // (Only declarations whose nodes merge_symbol() can point elsewhere)
    if (kind != SYMBOL_DECL_VARIABLE && kind != SYMBOL_DECL_FUNCTION && kind != SYMBOL_DECL_CLASS &&
        kind != SYMBOL_DECL_INTERFACE && kind != SYMBOL_DECL_ENUM && kind != SYMBOL_DECL_TYPE_ALIAS &&
        kind != SYMBOL_DECL_NAMESPACE
    ) {
        return nullptr;
    }
    SourceFile const* file = span_source(name_span);
    if (!file) return nullptr;
    // Another declaration of this file merges into the pending one (tsc's binder declares into the file's own)
    for (u32 i = 0; i < binder->pending_alias_merge_count; i++) {
        PendingAliasMerge const* p = &binder->pending_alias_merges[i];
        if (p->table != table || p->file != file || !csview_equals_sv(p->source->name, name)) continue;
        Symbol* source = p->source;
        *pending = true;
        if (source->flags & excludes) {
            report_conflicts(binder_errs(binder), source, name_span, includes, false, (csview){0});
            return binder_new_symbol(binder, name, includes);
        }
        source->decl_flags_before = source->flags;
        source->flags |= includes;
        *merged = true;
        return source;
    }
    Symbol* existing = symbol_table_get_own(table, name, symbol_name_hash(name));
    // (A UMD global, `export as namespace`, is merge_into_umd_globals()'s: tsc's first-in-wins globalExports)
    if (!existing || !(existing->flags & SYMBOL_ALIAS) || existing->binder != binder || (existing->flags & excludes) ||
        !existing->decl_count || is_umd_global(existing)
    ) {
        return nullptr;
    }
    for (u32 i = 0; i < existing->decl_count; i++) {
        if (span_source(existing->decls[i].full) == file) return nullptr;
    }
    Symbol* source = binder_new_symbol(binder, name, includes);
    PendingAliasMerge* p = list_push((void**)&binder->pending_alias_merges, &binder->pending_alias_merge_count,
        &binder->pending_alias_merge_cap, sizeof(PendingAliasMerge));
    *p = (PendingAliasMerge){ table, source, file };
    // (An expando assignment of the script declares into it: scope_init_declaration() records it in script_duplicates)
    *pending = true;
    *merged = false;
    return source;
}

void scope_init_declaration(Scope* me, Symbol** ret, DeclProps props) {
    if (*ret) {
        // `var`s of nested blocks were declared by statement_hoist_names() already
        if (!props.function_scoped) log_error("Declaration ", (*ret)->name_span, " acquired multiple times");
        return;
    }
    Scope* scope = me;
    if (props.function_scoped) {
        while (!scope_is_var_container(scope) && scope->parent) scope = scope->parent;
    }
    if (props.kind == SYMBOL_DECL_VARIABLE || props.kind == SYMBOL_DECL_FUNCTION || props.kind == SYMBOL_DECL_CLASS ||
        props.kind == SYMBOL_DECL_ENUM || props.kind == SYMBOL_DECL_NAMESPACE || props.kind == SYMBOL_DECL_INTERFACE ||
        props.kind == SYMBOL_DECL_TYPE_ALIAS || props.kind == SYMBOL_DECL_TYPE_PARAMETER
    ) {
        if (props.kind == SYMBOL_DECL_CLASS) scope_check_strict_class_name(me, props.name);
        else scope_check_strict_identifier(me, props.name, false);
    }
    // (Parameters and function names only outside ambient contexts, like tsc's bindParameter)
    if ((props.kind == SYMBOL_DECL_VARIABLE && !(props.is_parameter && me->ambient)) ||
        (props.kind == SYMBOL_DECL_FUNCTION && !me->ambient)
    ) {
        scope_check_strict_eval_or_arguments(me, props.name);
    }

    check_type_name_is_reserved(binder_errs(me->binder), props.name, props.kind);
    if (!props.is_ambient && (
        (props.kind == SYMBOL_DECL_VARIABLE && !props.is_parameter) || props.kind == SYMBOL_DECL_FUNCTION ||
        props.kind == SYMBOL_DECL_CLASS || props.kind == SYMBOL_DECL_ENUM ||
        (props.kind == SYMBOL_DECL_NAMESPACE && props.is_instantiated)
    )) {
        check_require_exports_collision(me, props.name, props.kind == SYMBOL_DECL_CLASS);
    }
    u32 includes, excludes;
    symbol_flags_of_props(&props, &includes, &excludes);
    if (props.default_export && !(scope->container && (scope->container->is_module || scope->container->is_augmentation))) {
        props.default_export = false; // TODO: TS1319 and friends
    }
    SymbolTable* table;
    csview name = props.symbol_name.buf ? props.symbol_name : span_sv(props.name);
    if (props.default_export) {
        symbol_ensure_exports(scope->container);
        table = scope->container->exports;
        name = c_sv("default");
    } else {
        table = scope_declaration_table(scope, props.exported);
    }
    bool merged;
    Span error_at = props.default_export && !span_valid(props.name) ? first_token(props.full) : props.name;
    // An exported declaration of a module or namespace has a local twin too (tsgo's declareModuleMember), declared with
    // the declaration's excludes: a local of the name declared before it that they exclude is a duplicate
    // (`class Foo {}` then `export type Foo = number`)
    if (!props.default_export && my container && table == my container->exports && table != scope->locals &&
        (scope->kind == SCOPE_MODULE || scope->kind == SCOPE_NAMESPACE)
    ) {
        Symbol const* local = symbol_table_get_own(scope_locals(scope), name, symbol_name_hash(name));
        if (local && (local->flags & excludes) && local->decl_count &&
            span_source(local->decls[0].name) == span_source(props.name)
        ) {
            report_conflicts(binder_errs(scope->binder), local, error_at, includes, false, (csview){0});
        }
    }
    bool pending_alias_merge = false;
    Symbol* symbol = props.default_export ? nullptr :
        declare_pending_alias_merge(scope, table, name, error_at, includes, excludes, props.kind, &merged, &pending_alias_merge);
    if (!symbol) symbol = declare_symbol(scope, table, name, error_at, includes, excludes, props.default_export, &merged);
    if (!merged && !pending_alias_merge && !props.default_export && (includes & SYMBOL_VALUE) && scope->container && table == scope->container->exports &&
        table != scope->locals && (scope->kind == SCOPE_MODULE || scope->kind == SCOPE_NAMESPACE) &&
        symbol_table_get_own(table, name, symbol_name_hash(name)) != symbol
    ) {
        // (The last one of the name wins: tsc sets the local's exportSymbol at each declaration)
        DuplicateExport* duplicate = arena_new(binder_arena(scope->binder), DuplicateExport);
        *duplicate = (DuplicateExport){ symbol, scope_rare(scope)->duplicate_exports };
        scope_rare_mut(scope)->duplicate_exports = duplicate;
    }
    if (!merged && scope->kind == SCOPE_SCRIPT && table == &scope->binder->globals &&
        symbol_table_get_own(table, name, symbol_name_hash(name)) != symbol
    ) {
        DuplicateExport* duplicate = arena_new(binder_arena(scope->binder), DuplicateExport);
        *duplicate = (DuplicateExport){ symbol, scope_rare(scope)->script_duplicates };
        scope_rare_mut(scope)->script_duplicates = duplicate;
    }
    u32 flags_before = merged ? symbol->decl_flags_before : 0;
    if (merged && includes & flags_before & SYMBOL_TYPE_PARAMETER && props.kind != SYMBOL_DECL_INFER) {
        // Type parameters of the same name merge as far as tsc's binder is concerned, which is what `infer U` twice
        // in one conditional type wants. <T, T>: one symbol declared twice (and one type), the second is TS2300
        report_conflict(binder_errs(scope->binder), props.name, 2300);
    }
    if (props.kind == SYMBOL_DECL_VARIABLE) {
        // (Also when it merges into something that isn't a variable: a `declare const never` and the type `never`)
        symbol->is_const = props.is_const;
        // (A `var` redeclaring a parameter is the parameter still: tsc's valueDeclaration is the first declaration)
        symbol->is_parameter = props.is_parameter || (merged && (flags_before & SYMBOL_VARIABLE) && symbol->is_parameter);
    }
    // (A function too: an ambient `require` function is CommonJS's, expr_symbol_is_commonjs_require())
    if (props.kind == SYMBOL_DECL_VARIABLE || props.kind == SYMBOL_DECL_CLASS || props.kind == SYMBOL_DECL_ENUM ||
        props.kind == SYMBOL_DECL_FUNCTION
    ) {
        // (A variable is as ambient as its first declaration, tsc's valueDeclaration: `var x; declare var x;` isn't)
        if (props.kind != SYMBOL_DECL_VARIABLE || !(flags_before & SYMBOL_VARIABLE)) {
            symbol->is_ambient |= scope->ambient || props.is_ambient;
        }
    }
    if (!merged && props.default_export) {
        // Named like its (first) declaration (tsc's getNameOfSymbolAsWritten), and exported like the declarations
        // `export default x` refers to
        symbol->name = span_valid(props.name) ? span_sv(props.name) : c_sv("default");
        symbol->name_span = props.name;
        symbol->is_default_export = true;
        symbol->referenced = SYMBOL_ANY_MEANING;
        symbol->parent = scope->container;
    } else if (props.default_export && !span_valid(symbol->name_span) && span_valid(props.name)) {
        // Named by its first named declaration
        symbol->name = span_sv(props.name);
        symbol->name_span = props.name;
    } else if (!merged) {
        // Like in tsc only exports have a parent: a local of a namespace is just `x`, not `N.x`
        if (scope->container && table == scope->container->exports) symbol->parent = scope->container;
        symbol->name_span = props.name;
        symbol->exported = props.exported;
        // (Not a conflict's symbol of its own, which isn't in the locals: tsc's unused check never sees it)
        if (table == scope->locals && symbol_table_get_own(table, name, symbol_name_hash(name)) == symbol) {
            symbol->is_local = true;
            symbol->function = scope->function;
        }
    }
    SymbolDecl decl = {
        .kind = props.kind, .flags = includes, .node = props.node, .name = props.name, .full = props.full, .block = scope,
        .hoisted = scope->hoisting, .written_export = props.exported || props.default_export,
        .written_declare = props.declare_keyword,
    };
    // A function of a module or namespace block declared both exported and not: tsc's declareModuleMember gives the
    // exported declarations a local too (ExportValue, which FunctionExcludes doesn't exclude), so the block's local
    // symbol has every declaration of the name and its type every signature, the export only the exported ones
    if (props.kind == SYMBOL_DECL_FUNCTION && !props.default_export && scope->container &&
        (scope->kind == SCOPE_MODULE || scope->kind == SCOPE_NAMESPACE) && scope->container->exports
    ) {
        if (table == scope->container->exports) {
            Symbol* local = symbol_table_get_own(scope_locals(scope), name, symbol_name_hash(name));
            if (local && local != symbol && (local->flags & SYMBOL_FUNCTION)) symbol_add_decl(local, decl);
        } else if (table == scope->locals && !merged) {
            Symbol* exported = symbol_table_get_own(scope->container->exports, name, symbol_name_hash(name));
            if (exported && exported != symbol && (exported->flags & SYMBOL_FUNCTION)) {
                for (u32 i = 0; i < exported->decl_count; i++) {
                    if (exported->decls[i].kind == SYMBOL_DECL_FUNCTION && exported->decls[i].block == scope) symbol_add_decl(symbol, exported->decls[i]);
                }
            }
        }
    }
    symbol_add_decl(symbol, decl);
    if (props.default_export && span_valid(props.name)) scope_declare_default_local(scope, symbol, props.name, excludes);
    if (includes & (SYMBOL_NAMESPACE_MODULE | SYMBOL_VALUE_MODULE | SYMBOL_ENUM)) symbol_ensure_exports(symbol);
    *ret = symbol;
}

// `export default class A`: a local `A` without meanings of its own, which conflicts with other local declarations of
// `A` like tsc's (the local symbol of declareModuleMember, only a value's has ExportValue). Looking `A` up finds the
// default export, see symbol_default_export_local_name()
static void scope_declare_default_local(Scope* me, Symbol* symbol, Span name, u32 excludes) {
    bool merged;
    Symbol* local = declare_symbol(me, scope_locals_mut(me), span_sv(name), name, SYMBOL_NONE, excludes, false, &merged);
    if (!merged) {
        local->name_span = name;
        local->is_local = true;
        local->referenced = SYMBOL_ANY_MEANING;
    }
    if (!symbol_rare(local)->export_symbol) symbol_rare_mut(local)->export_symbol = symbol;
    SymbolDecl decl = symbol->decls[symbol->decl_count - 1];
    decl.flags = SYMBOL_NONE;
    symbol_add_decl(local, decl);
}

// tsc's getDeclarationSpaces (without aliases, which would need resolving)
enum { SPACE_TYPE = 1, SPACE_VALUE = 2, SPACE_NAMESPACE = 4 };
// What an alias declaration's spaces depend on: tsc's getDeclarationSpaces resolves `import x = a.b`, default and
// namespace imports (an import specifier is a value)
static bool declaration_spaces_need_target(SymbolDecl const* decl) {
    if (decl->kind != SYMBOL_DECL_ALIAS || !decl->node || decl->flags != SYMBOL_ALIAS) return false;
    PortItem const* item = decl->node;
    return item->tag == PORT_IMPORT_ALIAS || item->tag == PORT_IMPORT_DEFAULT || item->tag == PORT_IMPORT_GATHER;
}

static u32 declaration_spaces(Symbol* owner, SymbolDecl const* decl, bool resolve) {
    switch (decl->kind) {
        case SYMBOL_DECL_INTERFACE:
        case SYMBOL_DECL_TYPE_ALIAS:
            return SPACE_TYPE;
        case SYMBOL_DECL_NAMESPACE:
            return decl->flags & SYMBOL_VALUE_MODULE ? SPACE_NAMESPACE | SPACE_VALUE : SPACE_NAMESPACE;
        case SYMBOL_DECL_CLASS:
        case SYMBOL_DECL_ENUM:
        case SYMBOL_DECL_ENUM_MEMBER:
            return SPACE_TYPE | SPACE_VALUE;
        case SYMBOL_DECL_VARIABLE:
        case SYMBOL_DECL_FUNCTION:
            return SPACE_VALUE;
        case SYMBOL_DECL_ALIAS: {
            // (An import specifier is a value whatever it imports: tsc's getDeclarationSpaces, microsoft/TypeScript#7591)
            if (decl->node && decl->flags == SYMBOL_ALIAS && ((PortItem const*)decl->node)->tag == PORT_IMPORT_SPECIFIC) {
                return SPACE_VALUE;
            }
            if (!resolve || !declaration_spaces_need_target(decl)) return 0;
            Symbol* target = symbol_resolve_alias(owner);
            if (!target || target->is_placeholder || target == owner) return 0;
            u32 ret = 0;
            for (u32 i = 0; i < target->decl_count; i++) ret |= declaration_spaces(target, &target->decls[i], false);
            return ret;
        }
        case SYMBOL_DECL_EXPR: {
            // `export default <expression>` exports a value; an entity name is an alias (tsc's getDeclarationSpaces)
            Expr const* expr = decl->node;
            while (expr && expr->tag == EXPR_PROPERTY && expr->property->form == PROPERTY_EXPR_DOT) expr = &expr->property->expr;
            return expr && expr->tag == EXPR_IDENTIFIER ? 0 : SPACE_VALUE;
        }
        default:
            return 0;
    }
}

static bool span_within(Span inner, Span outer) {
    return span_source(inner) == span_source(outer) && span_start_ptr(inner) >= span_start_ptr(outer) &&
        span_end_ptr(inner) <= span_end_ptr(outer);
}

typedef struct PendingExportMerge {
    Scope* scope;
    Span block;
    Symbol* local;
} PendingExportMerge;

// tsc's checkExportsOnMergedDeclarations for one local of a block. Without resolve, a local whose declarations are
// aliases that need their target waits for binder_link() (resolving during declaration memoises modules as missing)
static void check_export_merge(Scope* me, Span block, Symbol* local, bool resolve) {
    Symbol* exported = symbol_table_get(my container->exports, local->name);
    if (exported == local) exported = nullptr;
    if (!exported && !symbol_rare(local)->export_symbol) return;
    Symbol* symbols[] = { local, exported };
    if (!resolve) {
        bool needs_target = false;
        for (u32 s = 0; s < 2 && !needs_target; s++) {
            for (u32 d = 0; symbols[s] && d < symbols[s]->decl_count && !needs_target; d++) {
                needs_target = declaration_spaces_need_target(&symbols[s]->decls[d]);
            }
        }
        if (needs_target) {
            Binder* binder = my binder;
            ResolveShard* shard = binder_shard(binder);
            PendingExportMerge* pending = shard ?
                list_push((void**)&shard->pending_export_merges, &shard->pending_export_merge_count, &shard->pending_export_merge_cap, sizeof(PendingExportMerge)) :
                list_push((void**)&binder->pending_export_merges, &binder->pending_export_merge_count, &binder->pending_export_merge_cap, sizeof(PendingExportMerge));
            *pending = (PendingExportMerge){ me, block, local };
            return;
        }
    }
    // (tsc checks from variable, class, interface, type alias, enum and namespace declarations, not functions: a merge
    // of only functions is TS2383's, see checkFunctionOrConstructorSymbol)
    bool checked = false;
    for (u32 s = 0; s < 2 && !checked; s++) {
        for (u32 d = 0; symbols[s] && d < symbols[s]->decl_count && !checked; d++) {
            SymbolDecl const* decl = &symbols[s]->decls[d];
            if (s == 1 && !span_within(decl->full, block)) continue;
            checked = decl->kind == SYMBOL_DECL_VARIABLE || decl->kind == SYMBOL_DECL_CLASS ||
                decl->kind == SYMBOL_DECL_INTERFACE || decl->kind == SYMBOL_DECL_TYPE_ALIAS ||
                decl->kind == SYMBOL_DECL_ENUM || decl->kind == SYMBOL_DECL_NAMESPACE;
        }
    }
    if (!checked) return;
    // tsc's local symbol gets the exported declarations too (with no meaning of their own there, ExportValue excludes
    // nothing): an exported one that the block's earlier local declarations exclude is a duplicate, not TS2395
    for (u32 d = 0; exported && d < exported->decl_count; d++) {
        SymbolDecl const* decl = &exported->decls[d];
        // (Declarations with `export`, not export specifiers, which tsc doesn't declare locally)
        if (!span_within(decl->full, block) || decl->kind == SYMBOL_DECL_ALIAS || decl->kind == SYMBOL_DECL_EXPR ||
            decl->kind == SYMBOL_DECL_ASSIGNMENT || (decl->flags & SYMBOL_ALIAS) || decl->kind == SYMBOL_DECL_FUNCTION
        ) {
            continue; // (A function is bound first, tsc's bindEachFunctionsFirst: nothing local is declared before it)
        }
        u32 excludes = symbol_excludes(decl->flags), earlier = 0;
        for (u32 l = 0; l < local->decl_count; l++) {
            SymbolDecl const* other = &local->decls[l];
            if (other->flags && span_source(other->full) == span_source(decl->full) &&
                (span_start_ptr(other->full) < span_start_ptr(decl->full) || other->kind == SYMBOL_DECL_FUNCTION)
            ) {
                earlier |= other->flags;
            }
        }
        if (!(earlier & excludes)) continue;
        unsigned code = (earlier | decl->flags) & SYMBOL_ENUM ? 2567 : earlier & SYMBOL_BLOCK_SCOPED_VARIABLE ? 2451 : 2300;
        for (u32 l = 0; l < local->decl_count; l++) {
            SymbolDecl const* other = &local->decls[l];
            if (other->flags && span_source(other->full) == span_source(decl->full) &&
                (span_start_ptr(other->full) < span_start_ptr(decl->full) || other->kind == SYMBOL_DECL_FUNCTION)
            ) {
                report_conflict(binder_errs(my binder), other->name, code);
            }
        }
        Error* error = report_conflict(binder_errs(my binder), decl->name, code);
        // (tsc's declareSymbol: `export type T;` may have meant `export type { T }`)
        if (error && decl->kind == SYMBOL_DECL_TYPE_ALIAS && earlier & (SYMBOL_ALIAS | SYMBOL_TYPE | SYMBOL_NAMESPACE)) {
            TypeDecl const* alias = decl->node;
            if (alias->type.tag == TYPE_ANY && !alias->type.parsed) {
                error_add_related_d(error, decl->name, 1369, "Did you mean 'export type { ", decl->name, " }'?");
            }
        }
        return;
    }
    // The exported declarations of this block merge with its local ones (tsc: one local symbol), and so do those
    // of `export default class A` (the local's declarations without flags of their own)
    u32 exported_spaces = 0, local_spaces = 0, default_spaces = 0;
    for (u32 d = 0; exported && d < exported->decl_count; d++) {
        if (span_within(exported->decls[d].full, block)) exported_spaces |= declaration_spaces(exported, &exported->decls[d], resolve);
    }
    for (u32 d = 0; d < local->decl_count; d++) {
        // (In an ambient export context an import without `declare` is exported too: tsc's getEffectiveDeclarationFlags)
        SymbolDecl const* decl = &local->decls[d];
        if (my export_context && decl->kind == SYMBOL_DECL_ALIAS && decl->node && decl->flags == SYMBOL_ALIAS &&
            !((PortItem const*)decl->node)->declared
        ) {
            exported_spaces |= declaration_spaces(local, decl, resolve);
            continue;
        }
        if (local->decls[d].flags) local_spaces |= declaration_spaces(local, &local->decls[d], resolve);
        else default_spaces |= declaration_spaces(local, &local->decls[d], resolve);
    }
    u32 common = exported_spaces & local_spaces;
    u32 common_default = default_spaces & (exported_spaces | local_spaces);
    if (!common && !common_default) return;
    for (u32 s = 0; s < 2; s++) {
        for (u32 d = 0; symbols[s] && d < symbols[s]->decl_count; d++) {
            SymbolDecl const* decl = &symbols[s]->decls[d];
            if (s == 1 && !span_within(decl->full, block)) continue;
            if (!span_valid(decl->name)) continue;
            u32 spaces = declaration_spaces(symbols[s], decl, resolve);
            if (spaces & common_default) {
                error_ctx_error_d(binder_errs(my binder), decl->name, 2652, "Merged declaration '", decl->name,
                    "' cannot include a default export declaration. Consider adding a separate 'export default ",
                    decl->name, "' declaration instead.");
            } else if (spaces & common && (s == 1 || decl->flags)) {
                error_ctx_error_d(binder_errs(my binder), decl->name, 2395, "Individual declarations in merged declaration '",
                    decl->name, "' must be all exported or all local.");
            }
        }
    }
}

// A declaration file without `export {}` or `export =` exports everything (tsc's ExportContext), so its exported and
// local declarations of a name are one symbol here. tsc's getEffectiveDeclarationFlags still takes one with `declare`
// and no `export` as local: checkExportsOnMergedDeclarations' TS2395 when both kinds share a declaration space
static void check_export_context_merges(Scope* me, Span block) {
    for (u32 i = 0; i < my container->exports->count; i++) {
        Symbol* symbol = my container->exports->entries[i].symbol;
        if (symbol->decl_count < 2) continue;
        u32 exported_spaces = 0, local_spaces = 0;
        bool checked = false;
        for (u32 d = 0; d < symbol->decl_count; d++) {
            SymbolDecl const* decl = &symbol->decls[d];
            if (!span_within(decl->full, block) || decl->kind == SYMBOL_DECL_ALIAS || decl->kind == SYMBOL_DECL_EXPR ||
                decl->kind == SYMBOL_DECL_ASSIGNMENT || (decl->flags & SYMBOL_ALIAS)
            ) {
                continue;
            }
            checked |= decl->kind != SYMBOL_DECL_FUNCTION;
            bool exported = decl->written_export || !decl->written_declare;
            *(exported ? &exported_spaces : &local_spaces) |= declaration_spaces(symbol, decl, false);
        }
        u32 common = exported_spaces & local_spaces;
        if (!checked || !common) continue;
        for (u32 d = 0; d < symbol->decl_count; d++) {
            SymbolDecl const* decl = &symbol->decls[d];
            if (!span_within(decl->full, block) || decl->kind == SYMBOL_DECL_ALIAS || (decl->flags & SYMBOL_ALIAS) ||
                !span_valid(decl->name) || !(declaration_spaces(symbol, decl, false) & common)
            ) {
                continue;
            }
            error_ctx_error_d(binder_errs(my binder), decl->name, 2395, "Individual declarations in merged declaration '",
                decl->name, "' must be all exported or all local.");
        }
    }
}

void scope_check_export_merges(Scope* me, Span block) {
    if (!my container || !my container->exports || !span_valid(block)) return;
    for (u32 i = 0; i < scope_locals(me)->count; i++) check_export_merge(me, block, my locals->entries[i].symbol, false);
    if (my export_context && my file && my file->is_declaration) check_export_context_merges(me, block);
}

// `export default function () {}`: its first token (tsc's getErrorSpanForNode without a name)
static Span first_token(Span full) {
    if (!span_valid(full)) return full;
    isize len = 0;
    while (len < full.size && (isalnum((unsigned char)full.buf[len]) || full.buf[len] == '_')) len++;
    full.size = len ? len : 1;
    return full;
}

Span symbol_decl_error_span(SymbolDecl const* decl) {
    // `export default x` (tsc: the ExportAssignment)
    if (decl->kind == SYMBOL_DECL_EXPR || (decl->kind == SYMBOL_DECL_ALIAS && !decl->node)) return decl->full;
    return span_valid(decl->name) ? decl->name : first_token(decl->full);
}

static u32 excluded_flags(u32 flags);

// tsgo's hasShadowedNamespace: an `export =` alias the module's types make a namespace (bind_type_exports(), which
// binder_link() runs later) of a namespace that exports types or namespaces itself
static bool export_equals_shadows_namespace(Symbol const* module, Symbol* export_equals) {
    if (!(export_equals->flags & SYMBOL_ALIAS)) return false;
    bool promoted = false;
    for (u32 i = 0; i < module->exports->count && !promoted; i++) {
        Symbol const* other = module->exports->entries[i].symbol;
        promoted = other != export_equals && (other->flags & (SYMBOL_TYPE | SYMBOL_NAMESPACE));
    }
    if (!promoted) return false;
    Symbol const* target = symbol_resolve_alias(export_equals);
    if (!target || !(target->flags & SYMBOL_NAMESPACE) || !target->exports) return false;
    for (u32 i = 0; i < target->exports->count; i++) {
        if (symbol_resolved_flags(target->exports->entries[i].symbol) & (SYMBOL_TYPE | SYMBOL_NAMESPACE)) return true;
    }
    return false;
}

static void check_module_exports(Symbol const* container, ErrorCtx* errs) {
    if (!container || !container->exports) return;
    SymbolTable const* exports = container->exports;
    bool commonjs = container->is_module && container->module && container->module->file && container->module->file->is_commonjs;
    Symbol* export_equals = symbol_table_get(exports, c_sv("export="));
    if (export_equals && exports->count > 1 && export_equals->decl_count) {
        // (tsc's checkExternalModuleExports: only with other exports that are values, hasExportedMembersOfKind)
        bool values = false;
        for (u32 i = 0; i < exports->count && !values; i++) {
            Symbol* other = exports->entries[i].symbol;
            if (other == export_equals) continue;
            // (A CommonJS export's alias is a value: its target may be in a file not declared yet)
            values = (other->flags & SYMBOL_ALIAS && !commonjs ? symbol_resolved_flags(other) : other->flags | (other->flags & SYMBOL_ALIAS ? SYMBOL_VALUE : 0)) & SYMBOL_VALUE;
        }
        if (values || (container->is_module && export_equals_shadows_namespace(container, export_equals))) {
            error_ctx_error_d(errs, export_equals->decls[0].full, 2309,
                "An export assignment cannot be used in a module with other exported elements.");
        }
    }
    for (u32 i = 0; i < exports->count; i++) {
        Symbol const* symbol = exports->entries[i].symbol;
        if (symbol->flags & SYMBOL_NAMESPACE) continue; // (Namespaces and enums merge)
        // (Assignments to `exports.x` may repeat: tsc's JSDeclarationKindExportsProperty)
        bool exports_properties = commonjs;
        for (u32 d = 0; d < symbol->decl_count && exports_properties; d++) {
            SymbolDecl const* decl = &symbol->decls[d];
            exports_properties = decl->kind == SYMBOL_DECL_ALIAS || decl->kind == SYMBOL_DECL_EXPANDO ||
                (decl->kind == SYMBOL_DECL_ASSIGNMENT && decl->assignment == ASSIGNMENT_DECL_PROPERTY);
        }
        if (exports_properties) continue;
        u32 count = 0;
        for (u32 d = 0; d < symbol->decl_count; d++) {
            SymbolDecl const* decl = &symbol->decls[d];
            if (decl->kind == SYMBOL_DECL_INTERFACE || decl->kind == SYMBOL_DECL_EXPANDO) continue;
            if (decl->kind == SYMBOL_DECL_FUNCTION && !((FunctionImpl const*)decl->node)->has_body) continue;
            count++;
        }
        // A type alias may merge with one value
        if (count <= 1 || ((symbol->flags & SYMBOL_TYPE_ALIAS) && count <= 2)) continue;
        for (u32 d = 0; d < symbol->decl_count; d++) {
            SymbolDecl const* decl = &symbol->decls[d];
            if (decl->kind == SYMBOL_DECL_FUNCTION && !((FunctionImpl const*)decl->node)->has_body) continue;
            error_ctx_error_d(errs, symbol_decl_error_span(decl), 2323, "Cannot redeclare exported variable '", symbol_entry_name(&exports->entries[i]),
                "'.");
        }
    }
}

void scope_check_duplicate_exports(Scope* me) {
    check_module_exports(my container, binder_errs(my binder));
}

void binder_check_ambient_module_exports(Binder* me, ErrorCtx* errs) {
    for (u32 i = 0; i < my ambient_modules.count; i++) {
        Symbol const* module = my ambient_modules.entries[i].symbol;
        if (module->module && module->module->has_export_assignment) check_module_exports(module, errs);
    }
}

// Ports

static bool port_item_declares_export(PortItem const* item) {
    return port_item_is_export(item) || item->export_import;
}

void scope_add_port(Scope* me, PortItem* item) {
    switch (item->tag) {
        case PORT_NOOP:
            return;

        case PORT_EXPORT_AS_NAMESPACE: {
            // A global alias of the module (UMD; a module using its value is TS2686, check_umd_global_use()). The
            // checks are tsc's bindNamespaceExportDeclaration()
            if (span_valid(item->modifiers)) { // (at the whole declaration)
                Span whole = span_with_semicolon(item->full_span);
                if (span_start_ptr(item->modifiers) < span_start_ptr(whole)) {
                    whole = span_make_in(whole, span_start_ptr(item->modifiers), span_end_ptr(whole));
                }
                error_ctx_error_d(binder_errs(my binder), whole, 1184, "Modifiers cannot appear here.");
            }
            if (item->not_top_level) {
                error_ctx_error_d(binder_errs(my binder), span_with_semicolon(item->full_span), 1316, "Global module exports may only appear at top level.");
                return;
            } else if (my kind != SCOPE_MODULE) {
                error_ctx_error_d(binder_errs(my binder), span_with_semicolon(item->full_span), 1314, "Global module exports may only appear in module files.");
                return;
            } else if (my file && !my file->is_declaration) {
                error_ctx_error_d(binder_errs(my binder), span_with_semicolon(item->full_span), 1315, "Global module exports may only appear in declaration files.");
                return;
            }
            if (!my container) return;
            if (my file && my file->is_js) return; // TypeScript only (TS8xxx): tsc's symbols baseline has no alias there
            // Of two UMD globals of the same name the first one wins, silently (tsc's initializeTypeChecker): the
            // other is a symbol of its own that isn't global
            Symbol const* existing = symbol_table_get(&my binder->globals, port_item_name(item->decl_name));
            bool merged = false;
            Symbol* symbol;
            if (existing && existing->flags & SYMBOL_ALIAS && existing->alias && existing->alias->item &&
                existing->alias->item->tag == PORT_EXPORT_AS_NAMESPACE
            ) {
                symbol = binder_new_symbol(my binder, port_item_name(item->decl_name), SYMBOL_ALIAS);
            } else {
                symbol = declare_symbol(
                    me, &my binder->globals, port_item_name(item->decl_name), item->decl_name, SYMBOL_ALIAS, SYMBOL_ALIAS_EXCLUDES,
                    false, &merged
                );
            }
            if (merged) {
                // Merged into a global that isn't an alias yet (`declare global { namespace N {} }`): the alias
                // still needs a target, or resolving the symbol dereferences a null scope
                if (!symbol->alias->scope) {
                    symbol->alias->item = item;
                    symbol->alias->scope = me;
                }
                return;
            }
            symbol->name_span = item->decl_name;
            symbol_add_decl(symbol, (SymbolDecl){
                .kind = SYMBOL_DECL_ALIAS, .flags = SYMBOL_ALIAS, .node = item,
                .name = item->decl_name, .full = item->full_span,
            });
            symbol->alias->item = item;
            symbol->alias->scope = me;
            item->symbol = symbol;
            return;
        }

        case PORT_REEXPORT_SPLAT: {
            Symbol* module = my container;
            if (!module || !module->is_module || !item->module_ref) return;
            if (module->module->export_star_count == module->module->export_star_cap) {
                u32 cap = module->module->export_star_cap ? module->module->export_star_cap * 2 : 4;
                PortItem** stars = arena_new_n(binder_arena(my binder), PortItem*, cap);
                if (module->module->export_star_count) {
                    memcpy(stars, module->module->export_stars, sizeof(PortItem*) * module->module->export_star_count);
                }
                module->module->export_stars = stars;
                module->module->export_star_cap = cap;
            }
            module->module->export_stars[module->module->export_star_count++] = item;
            return;
        }

        case PORT_IMPORT_DEFAULT:
        case PORT_IMPORT_GATHER:
        case PORT_IMPORT_SPECIFIC:
        case PORT_IMPORT_ALIAS:
        case PORT_EXPORT_SPECIFIC:
        case PORT_REEXPORT_GATHER:
        case PORT_REEXPORT_SPECIFIC:
            break;
    }

    if (!port_item_is_export(item)) {
        // (Its local name: an imported or exported name is an identifier name, tsc's isIdentifierName)
        scope_check_strict_identifier(me, item->decl_name, false);
        if (!item->type_only) check_require_exports_collision(me, item->decl_name, false);
    }
    bool exported = port_item_declares_export(item);
    // (`declare global { export { a as b } }` declares globals, TS2666 aside)
    bool global_export = item->tag == PORT_EXPORT_SPECIFIC && my kind == SCOPE_GLOBAL_AUGMENTATION;
    // (In a function or block, TS1233: tsc's binder declares it in the block's locals all the same)
    bool block_export = port_item_is_export(item) && (my kind == SCOPE_FUNCTION || my kind == SCOPE_BLOCK);
    if (port_item_is_export(item) && !(my container && my kind != SCOPE_ENUM) && !global_export && !block_export) {
        return; // TODO: TS1194 export declarations are not permitted in a namespace... and nowhere else either
    }
    // (An alias is exported only by `export`: an ambient export context doesn't export imports, tsc's
    // declareModuleMember)
    SymbolTable* table = !exported && (my kind == SCOPE_MODULE || my kind == SCOPE_NAMESPACE) ? scope_locals_mut(me) :
        scope_declaration_table(me, exported);
    // (An import in a block, TS1232, too: in a script's block it is a global)
    bool block_import = !block_export && item->wrong_context && my kind == SCOPE_BLOCK;
    if (block_export || block_import) {
        // (tsc's declareSymbolAndAddToSymbolTable goes by the container: a block's is the function or file around it)
        Scope* container = me;
        while (container->kind == SCOPE_BLOCK && container->parent) container = container->parent;
        // (declareModuleMember puts an export specifier in a namespace's or module's exports)
        table = container->kind == SCOPE_SCRIPT ? scope_declaration_table(container, false) :
            block_export && (container->kind == SCOPE_NAMESPACE || container->kind == SCOPE_MODULE) && container->container ?
            scope_declaration_table(container, true) : scope_locals_mut(container);
    }
    bool merged;
    csview name = item->decl_name_missing ? c_sv("") : port_item_name(item->decl_name);
    bool default_export = exported && my container && my container->is_module && csview_equals(name, "default");
    Symbol* symbol = declare_symbol(
        me, table, name, item->decl_name, SYMBOL_ALIAS, SYMBOL_ALIAS_EXCLUDES, default_export, &merged
    );
    item->duplicate = !merged && symbol_table_get(table, name) != symbol;
    if (!merged) {
        symbol->parent = my container;
        symbol->name_span = item->decl_name;
        symbol->exported = exported;
        symbol->is_local = table == my locals;
    }
    symbol_add_decl(symbol, (SymbolDecl){
        .kind = SYMBOL_DECL_ALIAS, .flags = SYMBOL_ALIAS, .node = item, .name = item->decl_name, .full = item->full_span,
    });
    symbol->alias->item = item;
    symbol->alias->scope = me;
    item->symbol = symbol;

    bool needs_module = item->tag != PORT_IMPORT_ALIAS && item->tag != PORT_EXPORT_SPECIFIC;
    // (A source phase import has no module: its default binding is resolved like any alias, see resolve_alias_target())
    if (needs_module && !item->module_ref && !(item->source_phase && item->tag == PORT_IMPORT_DEFAULT)) {
        // The parser complained, or a source phase import's other bindings (TS18112), which are any
        symbol->alias->target = binder_new_placeholder(my binder, item->decl_name, item->full_span);
        symbol->alias->state = ALIAS_DONE;
    }
}

// tsc's isEntityNameExpression, parentheses nowhere: `a.b.c`
static bool is_unparenthesized_entity_name(Expr const* me) {
    if (my tag != EXPR_PROPERTY || !expr_is_entity_name(me)) return false;
    for (; my tag == EXPR_PROPERTY; me = &my property->expr) {
        if (expr_is_perenthisized(me)) return false;
    }
    return !expr_is_perenthisized(me);
}

// `export = v` / `export default v` in a function declaration's body (TS1231/TS1258): tsc's bindExportAssignment
// declares it in the exports of its container, the function, whose value then has a member `export=` or `default`
// of the expression's type (getTypeOfVariableOrParameterOrProperty of an ExportAssignment: unwidened). A name is an
// alias, a member only if it means a value (getNamedMembers: symbolIsValue)
static void declare_function_export(Scope* me, Expr* expr, bool export_equals);

bool scope_export_default_expr(Scope* me, Expr* expr, TypeNode const* jsdoc_type, Span statement, bool export_equals) {
    // tsc's checkExportAssignment -> checkGrammarModuleElementContext: only in a file or a namespace body (on the
    // first token; nothing is exported)
    bool top = my kind == SCOPE_MODULE || my kind == SCOPE_SCRIPT || my kind == SCOPE_GLOBAL_AUGMENTATION ||
        my kind == SCOPE_NAMESPACE || (my container && my container->is_module && my kind != SCOPE_BLOCK &&
            my kind != SCOPE_FUNCTION && my kind != SCOPE_STATIC_BLOCK);
    if (!top && span_valid(statement) && csview_starts_with(span_sv(statement), "export")) {
        Span first = span_make_in(statement, statement.buf, statement.buf + 6);
        if (export_equals) {
            error_ctx_grammar_error_d(binder_errs(my binder), first, 1231, "An export assignment must be at the top level of a file or module declaration.");
        } else {
            error_ctx_grammar_error_d(binder_errs(my binder), first, 1258, "A default export must be at the top level of a file or module declaration.");
        }
        declare_function_export(me, expr, export_equals);
        return false;
    }
    // (`export default x` in an augmentation merges into the module's default)
    if (!my container || !(my container->is_module || (my container->is_augmentation && !export_equals))) {
        return true; // TODO: TS1319 and friends
    }
    symbol_ensure_exports(my container);
    csview export_name = export_equals ? c_sv("export=") : c_sv("default");
    // Everything the name means is exported, types included; other expressions are a value. Nothing else can be
    // exported under the name (tsc's bindExportAssignment: SymbolFlags.All)
    bool alias = expr->tag == EXPR_IDENTIFIER && !expr->identifier.keyword;
    // (An entity name `a.b` or a class expression is an alias too: tsc's isAliasableExpression)
    bool entity = !alias && !jsdoc_type && is_unparenthesized_entity_name(expr);
    bool class_alias = !alias && !jsdoc_type && export_equals && expr->tag == EXPR_CLASS_DECL && !expr_is_perenthisized(expr);
    u32 includes = alias || entity || class_alias ? SYMBOL_ALIAS : SYMBOL_BLOCK_SCOPED_VARIABLE;
    bool merged;
    statement = span_with_semicolon(statement); // (tsc's ExportAssignment has it)
    // tsc's checkExportAssignment: `export =` can't be emitted as an ES module. A declaration file is only an ES module
    // when its format says so, an implementation file whenever it isn't CommonJS
    ModuleKind module_kind = compiler_options()->module;
    // (A JSON module's `export =` is only ours, tsc has no node for it)
    if (export_equals && my file && !my file->is_json && module_kind >= MODULE_KIND_ES2015 &&
        module_kind != MODULE_KIND_PRESERVE
    ) {
        ModuleKind implied = source_file_implied_format_for_emit(my file);
        if (my ambient ? implied == MODULE_KIND_ESNEXT : implied != MODULE_KIND_COMMONJS) {
            error_ctx_grammar_error_d(binder_errs(my binder), statement, 1203,
                "Export assignment cannot be used when targeting ECMAScript modules. Consider using 'export default' or "
                "another module format instead.");
        }
    }
    // (Not an augmentation: tsc skips those)
    if (my kind == SCOPE_NAMESPACE && my file && !my file->is_module && my container->module) {
        my container->module->has_export_assignment = true;
    }
    Span name = alias ? expr_outer_span(expr) : statement;
    if (expr->tag == EXPR_IMPLICIT_ANY && span_valid(expr_outer_span(expr)) && !expr_outer_span(expr).size) {
        // A missing expression is a missing identifier to tsc, at its full start (the end of `default`)
        char const* at = span_start_ptr(expr_outer_span(expr));
        char const* file_start = span_start_ptr(statement);
        while (at > file_start && (at[-1] == ' ' || at[-1] == '\t')) at--;
        name = span_make_in(expr_outer_span(expr), at, at);
    }
    Symbol* symbol = declare_symbol(
        me, my container->exports, export_name, name, includes, SYMBOL_ANY_MEANING | SYMBOL_ALIAS, !export_equals, &merged
    );
    if (!merged) {
        symbol->name = alias ? span_sv(expr->inner_span) : export_name;
        symbol->name_span = alias ? expr->inner_span : statement;
        symbol->parent = my container;
        symbol->is_default_export = !export_equals;
    }
    if (alias) {
        if (!symbol->alias) symbol->alias = arena_new(binder_arena(my binder), SymbolAlias);
        symbol->alias->scope = me;
        symbol->alias->name = expr->inner_span;
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_ALIAS, .flags = SYMBOL_ALIAS, .name = expr_outer_span(expr), .full = statement,
            .js = symbol_decl_js(my binder, jsdoc_type, false),
        });
    } else if (entity || class_alias) {
        if (!symbol->alias) symbol->alias = arena_new(binder_arena(my binder), SymbolAlias);
        symbol->alias->scope = me;
        if (entity) {
            symbol->alias->entity = expr;
        } else {
            ClassDecl* class_decl = expr->class_decl;
            if (!class_decl->name_scope) {
                class_decl->name_scope = scope_new(me, SCOPE_BLOCK);
                class_decl_add_names(class_decl, class_decl->name_scope, (DeclStatementProps){0});
            }
            symbol->alias->class_expr = class_decl;
            class_decl->export_assigned = true;
        }
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_ALIAS, .flags = SYMBOL_ALIAS, .name = statement, .full = statement,
        });
    } else {
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_EXPR, .flags = SYMBOL_BLOCK_SCOPED_VARIABLE, .node = expr,
            .name = statement, .full = statement, .js = symbol_decl_js(my binder, jsdoc_type, false),
        });
    }
    return true;
}

// CommonJS

// `exports.x = v`: an alias when v is a name (tsc's isAliasableExpression), else a property whose type is what is
// assigned (several assignments merge)
// A function expression assigned to a declared property (`exports.F = function () {}`, `ns.F = function () {}`) is a
// class of that symbol if it assigns `this.x` (tsc's bindThisPropertyAssignment: the function's own symbol)
static void declare_commonjs_export(
    Scope* me, csview name, Span name_span, Expr* value, TypeNode const* jsdoc_type, Span left, Span statement,
    AssignmentDeclKind kind, bool top_level, bool entity_ok
) {
    symbol_ensure_exports(my container);
    // (A class expression is an alias of its class, tsc's isAliasableExpression: at the top level, where the declare
    // step can make the class's symbol in the scope the resolve walk would)
    bool class_alias = value->tag == EXPR_CLASS_DECL && !expr_is_perenthisized(value) && top_level &&
        kind == ASSIGNMENT_DECL_PROPERTY && !jsdoc_type;
    // (`exports.x = a.b` too, of `exports` or `module.exports` itself: tsc's bindExportsPropertyAssignment)
    // (Only at the top level, where the alias's scope is the module's: elsewhere a property of the value's type)
    bool entity = top_level && entity_ok && !jsdoc_type && is_unparenthesized_entity_name(value);
    bool alias = (top_level && value->tag == EXPR_IDENTIFIER && !value->identifier.keyword) || class_alias || entity;
    u32 includes = alias ? SYMBOL_ALIAS : SYMBOL_PROPERTY;
    bool merged;
    Symbol* symbol = declare_symbol(me, my container->exports, name, name_span, includes, 0, false, &merged);
    if (!merged) {
        symbol->name = name;
        // (`export=` is named that, not by what is written)
        symbol->name_span = csview_equals(name, "export=") ? span_null : name_span;
        symbol->parent = my container;
        symbol->exported = true;
    }
    if (alias) {
        if (!symbol->alias) symbol->alias = arena_new(binder_arena(my binder), SymbolAlias);
        symbol->alias->scope = me;
        if (class_alias) {
            ClassDecl* class_decl = value->class_decl;
            if (!class_decl->name_scope) {
                class_decl->name_scope = scope_new(me, SCOPE_BLOCK);
                class_decl_add_names(class_decl, class_decl->name_scope, (DeclStatementProps){0});
            }
            symbol->alias->class_expr = class_decl;
            class_decl->commonjs_module = my container;
        } else if (entity) {
            symbol->alias->entity = value;
            symbol->alias->name = span_null;
        } else {
            symbol->alias->entity = nullptr;
            symbol->alias->name = value->inner_span;
        }
        SymbolDeclJs* js = symbol_decl_js(my binder, nullptr, true);
        js->commonjs_value = value;
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_ALIAS, .flags = SYMBOL_ALIAS, .name = left, .full = statement, .js = js,
        });
    } else {
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_ASSIGNMENT, .assignment = kind, .flags = SYMBOL_PROPERTY, .node = value, .name = left,
            .full = statement, .js = symbol_decl_js(my binder, jsdoc_type, true),
        });
    }
}

// `Object.defineProperty(exports, "x", {...})` (tsc's bindObjectDefinePropertyExport): an export declared by the call
static void add_define_property_export(Scope* me, Expr* call, bool top_level) {
    Span name;
    if (!expr_is_define_property_call(call, &name) || !expr_is_exports_object(vec_expr_at(&call->call->args, 0))) return;
    // (Named as written: a string literal with its quotes, tsgo's getNameOfSymbolAsWritten)
    Expr const* key = vec_expr_at(&call->call->args, 1);
    Span shown = key->literal.tag == VALUE_STRING ? key->inner_span : name;
    declare_commonjs_export(me, span_sv(name), shown, call, nullptr, expr_outer_span(call), expr_outer_span(call), ASSIGNMENT_DECL_DEFINE_PROPERTY, top_level, false);
    Symbol const* declares = symbol_table_get(my container->exports, span_sv(name));
    if (declares || call->call->rare) call_rare_mut(call->call)->declares = declares;
}

// Declared by the assignment expression (tsc's declaration node, `exports = module.exports = C` has two)
// `exports.x = v`, `module.exports.x = v` declare the export x, `module.exports = v` the `export=` (tsc's
// getAssignmentDeclarationKind: syntactic, wherever the assignment is, bindModuleExportsAssignment and
// bindExportsOrObjectDefineProperty)
static void add_commonjs_export(Scope* me, Expr* assignment, bool top_level) {
    BinaryOpExpr* op = assignment->binary_op;
    Span statement = expr_outer_span(assignment);
    Span name = span_null;
    switch (binary_commonjs_declaration(op, &name)) {
        case COMMONJS_TARGET_NONE:
            break;
        case COMMONJS_TARGET_EXPORTS_PROPERTY: {
            // (`exports["b"] = v` is named `"b"` where it's shown, like tsc's symbol of a string literal name)
            Span shown = op->left.tag == EXPR_PROPERTY && op->left.property->form == PROPERTY_EXPR_BRACKET &&
                op->left.property->index.tag == EXPR_LITERAL ? op->left.property->index.inner_span : name;
            declare_commonjs_export(me, span_sv(name), shown, &op->right, op->jsdoc_type, expr_outer_span(&op->left), statement, ASSIGNMENT_DECL_PROPERTY, top_level, true);
            // (The property access is the declaration, which a read in its flow container finds undefined before it:
            // expr_link_assignment_declaration())
            if (op->left.tag == EXPR_PROPERTY && op->left.property->form == PROPERTY_EXPR_DOT && !property_rare(op->left.property)->declares) {
                Symbol* exported = symbol_table_get(my container->exports, span_sv(name));
                if (exported && !(exported->flags & SYMBOL_ALIAS)) property_rare_mut(op->left.property)->declares = exported;
            }
        }   break;
        case COMMONJS_TARGET_MODULE_EXPORTS:
            // (`module.exports = exports` declares nothing)
            if (expr_is_plain_identifier(&op->right, "exports")) break;
        {
            // (`module.exports = exports = {}` exports what is assigned last, tsc's getRightMostAssignedExpression)
            Expr* value = &op->right;
            while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
                value = &value->binary_op->right;
            }
            declare_commonjs_export(me, c_sv("export="), expr_outer_span(&op->left), value, op->jsdoc_type, expr_outer_span(&op->left), statement, ASSIGNMENT_DECL_PROPERTY, top_level, true);
        }
            break;
    }
}

typedef struct CommonJsWalk {
    Scope* module;
    Expr const* top_level[64]; // The assignments of the current top-level expression statement (a chain's, a comma's)
    u32 top_level_count;
} CommonJsWalk;

static AstWalk commonjs_export_visit(Expr const* expr, void* arg) {
    CommonJsWalk* walk = arg;
    bool top_level = false;
    for (u32 i = 0; i < walk->top_level_count && !top_level; i++) top_level = walk->top_level[i] == expr;
    if (expr->tag == EXPR_BINARY_OP && expr->binary_op->op == BINARY_OP_EQUAL) {
        add_commonjs_export(walk->module, (Expr*)expr, top_level);
    } else if (expr->tag == EXPR_CALL) {
        add_define_property_export(walk->module, (Expr*)expr, top_level);
    }
    return AST_WALK_CONTINUE;
}

static void note_top_level_assignments(CommonJsWalk* walk, Expr const* expr) {
    if (walk->top_level_count == c_arraylen(walk->top_level)) return;
    if (expr->tag == EXPR_BINARY_OP && expr->binary_op->op == BINARY_OP_COMMA) {
        note_top_level_assignments(walk, &expr->binary_op->left);
        note_top_level_assignments(walk, &expr->binary_op->right);
    } else if (expr->tag == EXPR_BINARY_OP && expr->binary_op->op == BINARY_OP_EQUAL) {
        walk->top_level[walk->top_level_count++] = expr;
        note_top_level_assignments(walk, &expr->binary_op->right);
    } else if (expr->tag == EXPR_CALL) {
        walk->top_level[walk->top_level_count++] = expr;
    }
}

// The `module` and `exports` of a CommonJS file: variables of the file declared by the file itself, unless it declares
// them (tsc's declareCommonJSVariable). `module` is `{ exports: ... }`, `exports` what the module exports
static void declare_commonjs_variable(Scope* me, csview name, SymbolDeclKind kind) {
    if (symbol_table_get(scope_locals(me), name)) return;
    SourceFile* file = my file;
    Span at = span_from_sv(file, csview_from_n(source_file_start(file), 0));
    bool merged;
    Symbol* symbol = declare_symbol(me, scope_locals_mut(me), name, at, SYMBOL_FUNCTION_SCOPED_VARIABLE, 0, false, &merged);
    symbol->name = name;
    symbol->name_span = span_null; // (Not written anywhere)
    symbol->is_local = true;
    symbol_add_decl(symbol, (SymbolDecl){
        .kind = kind, .flags = SYMBOL_FUNCTION_SCOPED_VARIABLE, .node = file, .name = at, .full = at,
    });
}

void scope_add_commonjs_module(Scope* me) {
    if (my kind != SCOPE_MODULE || !my file || !my file->is_commonjs || !my container || !my block) return;
    CommonJsWalk walk = { .module = me };
    VecStatement* statements = &((Block*)my block)->statements;
    for_each (i, vec_statement, statements) {
        walk.top_level_count = 0;
        if (i->tag == STATEMENT_EXPR) note_top_level_assignments(&walk, &i->expr);
        ast_walk_statement(i, commonjs_export_visit, &walk);
        if (i->tag != STATEMENT_DECL_VARIABLE) continue;
        // `var m = module.exports = v`: the `export=` it declares first is named m (tsc's getNameOfSymbolAsWritten: a
        // declaration whose parent is a variable declaration)
        for_each (j, vec_initialized_binding, &i->decl.variable.initializers) {
            Expr const* value = initialized_binding_value(j);
            Symbol* declared = my container->exports ? symbol_table_get(my container->exports, c_sv("export=")) : nullptr;
            if (declared && j->binding.tag == BINDING_IDENTIFIER && value->tag == EXPR_BINARY_OP &&
                value->binary_op->op == BINARY_OP_EQUAL && expr_is_module_exports(&value->binary_op->left) &&
                declared->decl_count && span_eq(declared->decls[0].full, expr_outer_span(value))
            ) {
                declared->name_span = j->binding.content_span;
            }
        }
    }
    declare_commonjs_variable(me, c_sv("module"), SYMBOL_DECL_COMMONJS_MODULE);
    declare_commonjs_variable(me, c_sv("exports"), SYMBOL_DECL_COMMONJS_EXPORTS);
}

Symbol* binder_require_symbol(Binder* me) {
    if (!my require_symbol) {
        if (binder_shard(me)) log_error("the require symbol made by a resolve walk: program_resolve_walk() makes it first");
        my require_symbol = binder_new_symbol(me, c_sv("require"), SYMBOL_FUNCTION_SCOPED_VARIABLE);
        symbol_add_decl(my require_symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_BUILTIN, .flags = SYMBOL_FUNCTION_SCOPED_VARIABLE, .node = &type_any,
        });
    }
    return my require_symbol;
}

// Modules

static Symbol* source_file_module_symbol(SourceFile const* file) {
    return file && file->is_module && file->scope ? file->scope->container : nullptr;
}

// The name of a pattern ambient module is its string literal, quotes included
static bool pattern_module_matches(Symbol const* pattern, csview path, isize* prefix_len) {
    csview name = pattern->name;
    if (name.size < 2) return false;
    name = (csview){ name.buf + 1, name.size - 2 };
    const char* star = memchr(name.buf, '*', (size_t)name.size);
    if (!star) return false;
    isize prefix = star - name.buf;
    isize suffix = name.size - prefix - 1;
    if (path.size < prefix + suffix) return false;
    if (memcmp(path.buf, name.buf, (size_t)prefix)) return false;
    if (memcmp(path.buf + path.size - suffix, star + 1, (size_t)suffix)) return false;
    *prefix_len = prefix;
    return true;
}

// Import attributes against pattern ambient modules' (tsgo's tryResolvePatternAmbientModule), on the syntax: what a
// pattern's attributes are is their type literal, whose members the grammar wants to be `name: "string"` (TS1555).
// Other annotations are approximated: a union of strings, `string`, and what needs types to tell (a type reference)
// take any string, the rest none (hacks.md)

static csview unquoted(csview name) {
    if (name.size >= 2 && (name.buf[0] == '"' || name.buf[0] == '\'')) return csview_slice(name, 1, name.size - 1);
    return name;
}

// Whether a value (a string) is of a pattern attribute's annotation
static bool attribute_type_accepts(TypeNode const* type, ModuleAttr const* attr) {
    switch (type->tag) {
        case TYPE_VALUE:
            return type->value.tag == VALUE_STRING && attr->known && csview_equals_sv(cstr_sv(&type->value.string), attr->value);
        case TYPE_UNION:
            for_each (i, vec_type, &type->composite) {
                if (attribute_type_accepts(i, attr)) return true;
            }
            return false;
        case TYPE_STRING: case TYPE_ANY: case TYPE_UNKNOWN: case TYPE_IDENTIFIER: case TYPE_TEMPLATE: case TYPE_INDEXED:
        case TYPE_TERNARY: case TYPE_TYPEOF: case TYPE_KEYOF: case TYPE_INTERSECTION:
            return true;
        default:
            return false;
    }
}

static InterfaceMember const* attributes_member(TypeNode const* attributes, csview name) {
    for_each (i, vec_interface_member, &attributes->interface.members) {
        if (i->key.tag == INTERFACE_KEY_NAME && !i->is_method && csview_equals_sv(cstr_sv(&i->key.name_string), name)) return i;
    }
    return nullptr;
}

// The import's attributes type is assignable to the pattern's
static bool attributes_accept(TypeNode const* pattern, ModuleAttrs const* attributes) {
    if (!pattern) return true;
    for_each (i, vec_interface_member, &pattern->interface.members) {
        if (i->key.tag != INTERFACE_KEY_NAME || i->is_method) continue;
        ModuleAttr const* found = nullptr;
        for (u32 j = 0; j < attributes->count && !found; j++) {
            if (csview_equals_sv(attributes->items[j].name, cstr_sv(&i->key.name_string))) found = &attributes->items[j];
        }
        if (!found ? !i->optional : !attribute_type_accepts(&i->type, found)) return false;
    }
    return true;
}

// The strings an annotation is: a string literal type, or a union of them. False if it's something else
static bool attribute_type_strings(TypeNode const* type, csview* out, u32* count, u32 cap) {
    if (type->tag == TYPE_VALUE && type->value.tag == VALUE_STRING) {
        if (*count == cap) return false;
        out[(*count)++] = cstr_sv(&type->value.string);
        return true;
    }
    if (type->tag != TYPE_UNION) return false;
    for_each (i, vec_type, &type->composite) {
        if (!attribute_type_strings(i, out, count, cap)) return false;
    }
    return true;
}

// Annotation a is a subtype of b (identical included)
static bool attribute_type_subtype(TypeNode const* a, TypeNode const* b) {
    if (a == b) return true;
    csview strings[16];
    u32 count = 0;
    if (!attribute_type_strings(a, strings, &count, c_arraylen(strings))) return false;
    for (u32 i = 0; i < count; i++) {
        if (!attribute_type_accepts(b, &(ModuleAttr){ .value = strings[i], .known = true })) return false;
    }
    return true;
}

static u32 attributes_property_count(TypeNode const* attributes) {
    u32 ret = 0;
    if (!attributes) return 0;
    for_each (i, vec_interface_member, &attributes->interface.members) ret += i->key.tag == INTERFACE_KEY_NAME && !i->is_method;
    return ret;
}

// Pattern attributes a are a subtype of b's: every property of b is one of a's, of a subtype
static bool attributes_subtype(TypeNode const* a, TypeNode const* b) {
    if (!b) return true;
    for_each (i, vec_interface_member, &b->interface.members) {
        if (i->key.tag != INTERFACE_KEY_NAME || i->is_method) continue;
        InterfaceMember const* other = a ? attributes_member(a, cstr_sv(&i->key.name_string)) : nullptr;
        if (!other ? !i->optional : (other->optional && !i->optional) || !attribute_type_subtype(&other->type, &i->type)) return false;
    }
    return true;
}

static bool attributes_identical(TypeNode const* a, TypeNode const* b) {
    return attributes_property_count(a) == attributes_property_count(b) && attributes_subtype(a, b) && attributes_subtype(b, a);
}

void module_ref_syntactic_attributes(ModuleRef const* ref, ModuleAttrs* ret) {
    *ret = ref->attrs ? ref->attrs->attrs : (ModuleAttrs){0};
}

static void ast_attributes(struct ImportAttributes const* attributes, CallExpr const* call, ModuleAttrs* ret) {
    *ret = (ModuleAttrs){0};
    if (attributes) {
        for_each (i, vec_import_attribute, &attributes->elements) {
            if (ret->count == c_arraylen(ret->items)) break;
            bool known = i->value.tag == EXPR_LITERAL && i->value.literal.tag == VALUE_STRING;
            ret->items[ret->count++] = (ModuleAttr){
                .name = unquoted(span_sv(i->name)), .value = known ? cstr_sv(&i->value.literal.string) : c_sv(""), .known = known,
            };
        }
        return;
    }
    if (!call || vec_expr_size(&call->args) < 2) return;
    // `import("m", { with: { type: "css" } })` (parentheses are fine)
    Expr const* options = vec_expr_at(&call->args, 1);
    if (options->tag != EXPR_OBJECT) {
        ret->needs_types = true;
        return;
    }
    for_each (i, vec_object_expr_item, &options->object) {
        if (i->tag == OBJECT_ITEM_SPREAD || i->computed_key) {
            ret->needs_types = true;
            return;
        }
        if (!cstr_equals(&i->name_string, "with")) continue;
        if (i->tag != OBJECT_ITEM_PROPERTY || i->value.tag != EXPR_OBJECT) {
            ret->needs_types = true;
            return;
        }
        ret->count = 0;
        for_each (j, vec_object_expr_item, &i->value.object) {
            if (j->tag != OBJECT_ITEM_PROPERTY || j->computed_key) {
                ret->needs_types = true;
                return;
            }
            if (ret->count == c_arraylen(ret->items)) break;
            bool known = j->value.tag == EXPR_LITERAL && j->value.literal.tag == VALUE_STRING;
            if (!known) ret->needs_types = true;
            ret->items[ret->count++] = (ModuleAttr){
                .name = cstr_sv(&j->name_string), .value = known ? cstr_sv(&j->value.literal.string) : c_sv(""), .known = known,
            };
        }
    }
}

void module_ref_set_attributes(ModuleRef* ref, struct ImportAttributes const* attributes, CallExpr const* call) {
    ModuleAttrs found;
    ast_attributes(attributes, call, &found);
    module_ref_attrs_free(ref->attrs);
    ref->attrs = nullptr;
    if (!found.count && !found.needs_types) return;
    isize size = 0;
    for (u32 i = 0; i < found.count; i++) size += found.items[i].name.size + found.items[i].value.size;
    ModuleRefAttrs* copy = c_new(ModuleRefAttrs, { .attrs = found });
    copy->storage = malloc((size_t)size + 1);
    panic_if(!copy->storage, "out of memory");
    char* at = copy->storage;
    for (u32 i = 0; i < found.count; i++) {
        ModuleAttr* item = &copy->attrs.items[i];
        memcpy(at, item->name.buf, (size_t)item->name.size);
        item->name.buf = at;
        at += item->name.size;
        memcpy(at, item->value.buf, (size_t)item->value.size);
        item->value.buf = at;
        at += item->value.size;
    }
    ref->attrs = copy;
}

bool binder_has_attributed_patterns(Binder const* me) {
    for (Binder const* binder = me; binder; binder = binder->parent) {
        if (binder->attributed_patterns) return true;
    }
    return false;
}

// tsgo's tryResolvePatternAmbientModule: the pattern ambient modules that match the name and whose attributes the
// import's are assignable to; the most specific attributes, then the longest prefix. Null if none
static Symbol* best_pattern_module(Binder const* me, csview path, ModuleAttrs const* attributes) {
    Symbol* candidates[16];
    u32 count = 0;
    for (Binder const* binder = me; binder; binder = binder->parent) {
        for (u32 i = 0; i < binder->pattern_module_count && count < c_arraylen(candidates); i++) {
            Symbol* pattern = binder->pattern_modules[i];
            isize prefix;
            if (pattern_module_matches(pattern, path, &prefix) && attributes_accept(pattern->module->attributes, attributes)) {
                candidates[count++] = pattern;
            }
        }
    }
    // (Dropping those whose attributes another's are a strict subtype of)
    Symbol* best = nullptr;
    isize best_prefix = -1;
    for (u32 i = 0; i < count; i++) {
        TypeNode const* own = candidates[i]->module->attributes;
        bool dominated = false;
        for (u32 j = 0; j < count && !dominated; j++) {
            TypeNode const* other = candidates[j]->module->attributes;
            dominated = i != j && other != own && attributes_subtype(other, own) && !attributes_subtype(own, other);
        }
        isize prefix;
        if (!dominated && pattern_module_matches(candidates[i], path, &prefix) && prefix > best_prefix) {
            best = candidates[i];
            best_prefix = prefix;
        }
    }
    return best;
}

Symbol* binder_find_module_with(Binder const* me, ModuleRef const* ref, ModuleAttrs const* attributes) {
    csview path = cstr_sv(&ref->path);
    bool relative = csview_starts_with(path, "./") || csview_starts_with(path, "../") ||
        csview_equals(path, ".") || csview_equals(path, "..");
    // Attributes make a pattern ambient module win over the ambient module or file of the name (tsgo's
    // tryResolvePatternAmbientModule with a resolved symbol)
    bool attributed = attributes->count;
    Symbol* resolved = nullptr;
    if (!relative && !ref->resolved_before_ambient) resolved = symbol_table_get(&my ambient_modules, path);
    if (resolved && !attributed) return resolved;
    if (!resolved && ref->file) {
        resolved = source_file_module_symbol(ref->file);
        if (!attributed) return resolved;
    }
    Symbol* best = best_pattern_module(me, path, attributes);
    if (!best) return resolved;
    Symbol* augmented = symbol_table_get_own(&me->pattern_augmentations, path, symbol_name_hash(path));
    if (augmented) {
        // (What it augments is the pattern without attributes, tsgo's mergeModuleAugmentation: known once some import
        // asks, its exports are the parent of the name's own)
        Symbol* target = attributed ? best_pattern_module(me, path, &(ModuleAttrs){0}) : best;
        if (target && !augmented->exports->parent) augmented->exports->parent = target->exports;
        if (best == target) return augmented;
    }
    return best;
}

static Symbol* binder_find_module(Binder const* me, ModuleRef const* ref) {
    ModuleAttrs attributes;
    module_ref_syntactic_attributes(ref, &attributes);
    return binder_find_module_with(me, ref, &attributes);
}

Symbol* binder_module_of_ref(Binder const* me, ModuleRef* ref) {
    if (!ref) return nullptr;
    if (ref->module_state != ALIAS_DONE) {
        ref->module = binder_find_module(me, ref);
        ref->module_state = ALIAS_DONE;
    }
    return ref->module;
}

Symbol* module_export_equals(Symbol* module) {
    if (!module || !module->is_module) return module;
    if (symbol_rare(module)->export_equals_clone) return symbol_rare(module)->export_equals_clone;
    Symbol* assigned = symbol_table_get(module->exports, c_sv("export="));
    if (!assigned) return module;
    Symbol* target = assigned->flags & SYMBOL_ALIAS ? symbol_resolve_alias(assigned) : assigned;
    if (!target) {
        // Its `export =` is being resolved: the import that reached it back is circular, and gets tsc's
        // unknownSymbol (resolveAlias's inner visit) like the `export =` itself will
        Symbol* unknown = binder_new_placeholder(assigned->binder, assigned->name_span, assigned->name_span);
        unknown->is_unresolved = true;
        return unknown;
    }
    // (`export = C.B` of a static B and a namespace's interface B: the alias, which has both meanings)
    if (assigned->flags & SYMBOL_ALIAS && assigned->alias->value_target) return assigned;
    // (So is an `export = C` that the module's types made a namespace, bind_type_exports())
    if (assigned->flags & SYMBOL_ALIAS && assigned->flags & SYMBOL_NAMESPACE_MODULE) return assigned;
    return target;
}

static i32 symbol_table_own_index(SymbolTable const* me, csview name, u64 hash);

static bool module_visited(Symbol** visited, u32 count, Symbol const* module) {
    for (u32 i = 0; i < count; i++) {
        if (visited[i] == module) return true;
    }
    return false;
}

typedef struct ExportStarsCtx {
    SymbolTable* target;
    Arena* arena;
    Symbol** visited;
    u32 visited_count;
    u32 visited_cap;
    // tsc's typeOnlyExportStarMap: the names an `export type *` brought, with the outermost such star (the last one
    // to see the name), and the modules reached through no type-only star, whose own names are not type-only
    SymbolTable type_only; // Names only, the symbols are unused
    PortItem const** type_only_star; // Parallel to type_only's entries (malloc'd)
    u32 type_only_star_cap;
    Symbol** value_modules; // malloc'd
    u32 value_module_count;
    u32 value_module_cap;
} ExportStarsCtx;

// tsc's extendExportStars(): own exports shadow what comes through `export *`, of two stars the first wins, `default`
// doesn't go through a star. type_only_star: the outermost `export type *` the module is reached through
static void extend_export_stars(ExportStarsCtx* ctx, Symbol* module, bool nested, PortItem const* type_only_star) {
    if (!type_only_star) {
        // (Before the visited check, like tsc's nonTypeOnlyNames)
        if (ctx->value_module_count == ctx->value_module_cap) {
            ctx->value_module_cap = ctx->value_module_cap ? ctx->value_module_cap * 2 : 8;
            ctx->value_modules = realloc(ctx->value_modules, sizeof(Symbol*) * ctx->value_module_cap);
        }
        ctx->value_modules[ctx->value_module_count++] = module;
    }
    if (module_visited(ctx->visited, ctx->visited_count, module)) return;
    if (ctx->visited_count == ctx->visited_cap) {
        ctx->visited_cap = ctx->visited_cap ? ctx->visited_cap * 2 : 16;
        ctx->visited = realloc(ctx->visited, sizeof(Symbol*) * ctx->visited_cap);
    }
    ctx->visited[ctx->visited_count++] = module;

    u32 count;
    SymbolEntry* own = symbol_table_collect(module->exports, &count);
    for (u32 i = 0; i < count; i++) {
        if (nested && (csview_equals(symbol_entry_name(&own[i]), "default") || csview_equals(symbol_entry_name(&own[i]), "export="))) continue;
        if (!symbol_table_get_own(ctx->target, symbol_entry_name(&own[i]), own[i].hash)) {
            symbol_table_insert(ctx->target, ctx->arena, symbol_entry_name(&own[i]), own[i].hash, own[i].symbol);
        }
        if (type_only_star) {
            i32 at = symbol_table_own_index(&ctx->type_only, symbol_entry_name(&own[i]), own[i].hash);
            if (at < 0) {
                symbol_table_insert(&ctx->type_only, ctx->arena, symbol_entry_name(&own[i]), own[i].hash, own[i].symbol);
                at = (i32)ctx->type_only.count - 1;
                if (ctx->type_only.count > ctx->type_only_star_cap) {
                    ctx->type_only_star_cap = ctx->type_only_star_cap ? ctx->type_only_star_cap * 2 : 16;
                    ctx->type_only_star = realloc(ctx->type_only_star, sizeof(PortItem const*) * ctx->type_only_star_cap);
                }
            }
            ctx->type_only_star[at] = type_only_star;
        }
    }
    free(own);

    for (u32 i = 0; i < module->module->export_star_count; i++) {
        PortItem const* star = module->module->export_stars[i];
        Symbol* next = binder_module_of_ref(module->binder, star->module_ref);
        if (!next) continue;
        extend_export_stars(ctx, next, true, type_only_star ? type_only_star : star->type_only ? star : nullptr);
    }
}

// The position of the name among the table's own entries, -1 if it isn't there
static i32 symbol_table_own_index(SymbolTable const* me, csview name, u64 hash) {
    name = name_cooked(name);
    if (!my index) {
        for (u32 i = 0; i < my count; i++) {
            if (entry_matches(&my entries[i], name, hash)) return (i32)i;
        }
        return -1;
    }
    u32 mask = my index_cap - 1;
    for (u32 pos = (u32)hash & mask; my index[pos]; pos = (pos + 1) & mask) {
        if (entry_matches(&my entries[my index[pos] - 1], name, hash)) return (i32)(my index[pos] - 1);
    }
    return -1;
}

static Symbol const* resolve_for_comparison(Symbol* symbol) {
    Symbol* ret = symbol && symbol->flags & SYMBOL_ALIAS ? symbol_resolve_alias(symbol) : symbol;
    return ret ? ret : symbol;
}

typedef struct ExportStarWalk {
    Arena arena;
    Symbol** visited;
    u32 visited_count;
    u32 visited_cap;
    ErrorCtx* errs;
} ExportStarWalk;

// tsc's getExportsOfModuleWorker visit(): the exports of a module with those of its `export *`s (own exports win, of
// two stars the first one). Where two stars of the module being checked (report) export different things by one name
// that it doesn't export itself, the later star is TS2308
static SymbolTable const* export_star_visit(ExportStarWalk* walk, Symbol* module, bool report) {
    if (!module || !module->exports || module_visited(walk->visited, walk->visited_count, module)) return nullptr;
    if (walk->visited_count == walk->visited_cap) {
        walk->visited_cap = walk->visited_cap ? walk->visited_cap * 2 : 16;
        walk->visited = realloc(walk->visited, sizeof(Symbol*) * walk->visited_cap);
    }
    walk->visited[walk->visited_count++] = module;
    u32 star_count = module->is_module ? module->module->export_star_count : 0;
    if (!star_count) return module->exports;

    SymbolTable* symbols = arena_new(&walk->arena, SymbolTable);
    u32 count;
    SymbolEntry* own = symbol_table_collect(module->exports, &count);
    for (u32 i = 0; i < count; i++) symbol_table_insert(symbols, &walk->arena, symbol_entry_name(&own[i]), own[i].hash, own[i].symbol);
    free(own);

    SymbolTable nested = {0};
    PortItem const** first_star = nullptr; // Parallel to nested's entries
    u32 first_star_cap = 0;
    // Pairs of (nested entry index, the later star), in order
    struct { u32 entry; PortItem const* star; }* dups = nullptr;
    u32 dup_count = 0, dup_cap = 0;
    for (u32 s = 0; s < star_count; s++) {
        PortItem const* star = module->module->export_stars[s];
        // (The module itself, not what its `export =` names: tsc's resolveExternalModuleName, TS2498 there)
        Symbol* target = binder_module_of_ref(module->binder, star->module_ref);
        SymbolTable const* exported = export_star_visit(walk, target, false);
        if (!exported) continue;
        SymbolEntry* entries = symbol_table_collect(exported, &count);
        for (u32 i = 0; i < count; i++) {
            if (csview_equals(symbol_entry_name(&entries[i]), "default")) continue;
            i32 at = symbol_table_own_index(&nested, symbol_entry_name(&entries[i]), entries[i].hash);
            if (at < 0) {
                symbol_table_insert(&nested, &walk->arena, symbol_entry_name(&entries[i]), entries[i].hash, entries[i].symbol);
                if (nested.count > first_star_cap) {
                    first_star_cap = first_star_cap ? first_star_cap * 2 : 16;
                    first_star = realloc(first_star, sizeof(PortItem const*) * first_star_cap);
                }
                first_star[nested.count - 1] = star;
            } else if (report && resolve_for_comparison(nested.entries[at].symbol) != resolve_for_comparison(entries[i].symbol)) {
                if (dup_count == dup_cap) {
                    dup_cap = dup_cap ? dup_cap * 2 : 8;
                    dups = realloc(dups, sizeof(*dups) * dup_cap);
                }
                dups[dup_count++] = (typeof(*dups)){ (u32)at, star };
            }
        }
        free(entries);
    }
    for (u32 e = 0; e < nested.count && dup_count; e++) {
        SymbolEntry const* entry = &nested.entries[e];
        if (csview_equals(symbol_entry_name(entry), "export=") || symbol_table_get_own(symbols, symbol_entry_name(entry), entry->hash)) continue;
        for (u32 d = 0; d < dup_count; d++) {
            if (dups[d].entry != e || !span_valid(dups[d].star->full_span)) continue;
            Span at = span_with_semicolon(dups[d].star->full_span);
            if (!span_source(at) || span_source(at)->is_stdlib) continue;
            error_ctx_error_d(walk->errs, at, 2308, "Module ", first_star[e]->module_ref->span,
                " has already exported a member named '", symbol_entry_name(entry),
                "'. Consider explicitly re-exporting to resolve the ambiguity.");
        }
    }
    for (u32 e = 0; e < nested.count; e++) {
        SymbolEntry const* entry = &nested.entries[e];
        if (!symbol_table_get_own(symbols, symbol_entry_name(entry), entry->hash)) {
            symbol_table_insert(symbols, &walk->arena, symbol_entry_name(entry), entry->hash, entry->symbol);
        }
    }
    free(first_star);
    free(dups);
    return symbols;
}

void binder_check_export_star_conflicts(Binder* me, ErrorCtx* errs) {
    for (u32 i = 0; i < my symbol_count; i++) {
        Symbol* module = my symbols[i];
        if (!module->is_module || !module->module || !module->module->export_star_count) continue;
        ExportStarWalk walk = { .errs = errs };
        export_star_visit(&walk, module_export_equals(module), true);
        free(walk.visited);
        arena_drop(&walk.arena);
    }
}

static SymbolTable const* module_resolved_exports(Symbol* me) {
    if (!my module->export_star_count) return my exports;
    if (my module->resolved_exports_state == ALIAS_DONE) return my module->resolved_exports;
    SymbolTable* table = arena_new(binder_arena(my binder), SymbolTable);
    ExportStarsCtx ctx = { .target = table, .arena = binder_arena(my binder) };
    extend_export_stars(&ctx, me, false, nullptr);
    // What stays type-only: the names no module reached without a type-only star exports
    for (u32 i = 0; i < ctx.type_only.count; i++) {
        SymbolEntry const* entry = &ctx.type_only.entries[i];
        bool value = false;
        for (u32 m = 0; m < ctx.value_module_count && !value; m++) {
            value = symbol_table_get_own(ctx.value_modules[m]->exports, symbol_entry_name(entry), entry->hash);
        }
        if (value) continue;
        if (!my module->type_only_exports) my module->type_only_exports = arena_new(binder_arena(my binder), SymbolTable);
        SymbolTable* names = my module->type_only_exports;
        symbol_table_insert(names, binder_arena(my binder), symbol_entry_name(entry), entry->hash, entry->symbol);
        if (names->count > my module->type_only_star_cap) {
            u32 cap = my module->type_only_star_cap ? my module->type_only_star_cap * 2 : 8;
            PortItem const** stars = arena_new_n(binder_arena(my binder), PortItem const*, cap);
            if (names->count > 1) memcpy(stars, my module->type_only_stars, sizeof(PortItem const*) * (names->count - 1));
            my module->type_only_stars = stars;
            my module->type_only_star_cap = cap;
        }
        my module->type_only_stars[names->count - 1] = ctx.type_only_star[i];
    }
    free(ctx.visited);
    free(ctx.type_only_star);
    free(ctx.value_modules);
    my module->resolved_exports = table;
    my module->resolved_exports_state = ALIAS_DONE;
    return table;
}

PortItem const* module_type_only_star(Symbol* module, csview name) {
    if (!module || !module->is_module || !module->module->export_star_count) return nullptr;
    module_resolved_exports(module);
    SymbolTable const* names = module->module->type_only_exports;
    if (!names) return nullptr;
    i32 at = symbol_table_own_index(names, name, symbol_name_hash(name));
    return at < 0 ? nullptr : module->module->type_only_stars[at];
}

Symbol* symbol_get_export(Symbol const* me, csview name) {
    me = symbol_members(me);
    if (!me) return nullptr;
    SymbolTable const* table = my is_module ? module_resolved_exports((Symbol*)me) : my exports;
    return symbol_table_get(table, name);
}

SymbolTable const* symbol_exports_table(Symbol const* me) {
    me = symbol_members(me);
    if (!me) return nullptr;
    return my is_module ? module_resolved_exports((Symbol*)me) : my exports;
}

SymbolEntry* symbol_collect_exports(Symbol const* me, u32* count) {
    *count = 0;
    me = symbol_members(me);
    if (!me) return nullptr;
    return symbol_table_collect(my is_module ? module_resolved_exports((Symbol*)me) : my exports, count);
}

// Aliases

static bool symbol_is_pure_alias(Symbol const* me) {
    return (my flags & (SYMBOL_ALIAS | SYMBOL_ANY_MEANING)) == SYMBOL_ALIAS && !my is_placeholder;
}

// What tsc's resolveSymbol() does to the thing an alias names: only an alias that is nothing else is seen through
static Symbol* resolve_if_pure_alias(Symbol* me) {
    if (!me || !symbol_is_pure_alias(me)) return me;
    Symbol* target = symbol_resolve_alias(me);
    // An alias whose meanings come from two symbols stays: symbol_for_meaning() picks (see SymbolAlias.value_target)
    return my alias->value_target ? me : target;
}

static Symbol* resolve_import_alias_path(Symbol* me) {
    PortItem* item = my alias->item;
    ErrorCtx* errs = my alias->scope->binder->errs;
    if (vec_span_is_empty(&port_item_alias(item)->path)) return nullptr;
    Span first = *vec_span_front(&port_item_alias(item)->path);
    if (!first.size) return nullptr; // (A missing name: tsc's resolveEntityName says nothing)
    Symbol* target;
    if (vec_span_size(&port_item_alias(item)->path) == 1) {
        // (tsc's getSymbolOfPartOfRightHandSideOfImportEquals: a namespace)
        target = scope_resolve(my alias->scope, span_sv(first), SYMBOL_NAMESPACE);
        if (target == me) target = nullptr;
        if (!target) {
            Symbol const* other = scope_resolve(my alias->scope, span_sv(first), SYMBOL_VALUE | SYMBOL_TYPE);
            if (other == me) other = nullptr;
            if (!other) {
                // (tsc's getSymbolOfPartOfRightHandSideOfImportEquals: a namespace meaning, with a spelling suggestion,
                // whose candidates' aliases are resolved: an alias in flight is TS2303 then)
                report_misspelled_or(my alias->scope, first, span_sv(first), SYMBOL_NAMESPACE, true, FALLBACK_NAMESPACE, errs);
            } else if (symbol_resolved_flags(other) & SYMBOL_TYPE) {
                error_ctx_error_d(errs, first, 2702, "'", first, "' only refers to a type, but is being used as a namespace here.");
            } else {
                error_ctx_error_d(errs, first, 2503, "Cannot find namespace '", first, "'.");
            }
            return nullptr;
        }
        unused_mark_referenced(my alias->scope, target, SYMBOL_ANY_MEANING);
        vec_symbol_cptr_push(&item->alias_->path_symbols, target);
        return resolve_if_pure_alias(target);
    }

    target = scope_resolve(my alias->scope, span_sv(first), SYMBOL_NAMESPACE);
    if (target == me) target = nullptr;
    if (!target) {
        error_ctx_error_d(errs, first, 2503, "Cannot find namespace '", first, "'.");
        return nullptr;
    }
    unused_mark_referenced(my alias->scope, target, SYMBOL_NAMESPACE);
    vec_symbol_cptr_push(&item->alias_->path_symbols, target);
    target = resolve_if_pure_alias(target);
    for (isize i = 1; target && i < vec_span_size(&port_item_alias(item)->path); i++) {
        Span name = *vec_span_at(&port_item_alias(item)->path, i);
        if (!name.size) return nullptr; // (A missing name)
        bool last = i == vec_span_size(&port_item_alias(item)->path) - 1;
        Symbol const* container = symbol_for_meaning(target, SYMBOL_NAMESPACE);
        if (!container || container->is_placeholder) return nullptr; // A placeholder: anything goes
        Symbol* found = (Symbol*)symbol_namespace_member(container, span_sv(name));
        Symbol* next = resolve_if_pure_alias(found);
        // (An export that is an alias resolving to nothing, a circular one (TS2303 there): found, unknown, nothing more
        // to say, tsc's resolveEntityName gets unknownSymbol from resolveAlias)
        if (found && !next) return nullptr;
        if (!next || (!last && !(symbol_resolved_flags(next) & SYMBOL_NAMESPACE) && !next->is_placeholder)) {
            if (next && !last && (symbol_resolved_flags(next) & SYMBOL_TYPE)) {
                // An exported type used as a namespace (tsc's resolveEntityName): said at what follows it
                Span right = *vec_span_at(&port_item_alias(item)->path, i + 1);
                error_ctx_error_d(errs, right, 2713, "Cannot access '", name, ".", right, "' because '", name,
                    "' is a type, but not a namespace. Did you mean to retrieve the type of the property '", right,
                    "' in '", name, "' with '", name, "[\"", right, "\"]'?");
                return nullptr;
            }
            cstr qualified = symbol_fully_qualified_name(container);
            if (!scope_report_misspelled_export(container, name, cstr_sv(&qualified), false, errs)) {
                error_ctx_error_d(errs, name, 2694, "Namespace '", qualified, "' has no exported member '", name, "'.");
            }
            cstr_drop(&qualified);
            return nullptr;
        }
        target = next;
        vec_symbol_cptr_push(&item->alias_->path_symbols, found);
    }
    return target;
}

// `import * as ns`: a namespace symbol of its own around the module, so that its type prints as `typeof ns` (tsc gets
// there through symbol accessibility, plan 04 stage 3)
static bool esm_imports_cjs(PortItem const* item, SourceFile const* target);

static Symbol* make_gather_target(Symbol* me, Symbol* module) {
    PortItem* item = my alias->item;
    Symbol* ret = binder_new_symbol(my binder, port_item_name(item->decl_name), SYMBOL_VALUE_MODULE);
    ret->name_span = item->decl_name;
    ModuleKind module_kind = compiler_options()->module;
    SourceFile const* file = item->module_ref ? item->module_ref->file : nullptr;
    if (item->tag == PORT_IMPORT_GATHER && file && !file->is_json && module->is_module && module->module->file &&
        module_kind >= MODULE_KIND_NODE16 && module_kind <= MODULE_KIND_NODENEXT && esm_imports_cjs(item, file)
    ) {
        // An ES module's `import * as ns` of a CommonJS one in Node.js: the module's exports, and in its type a
        // synthetic `default` that is the module, overriding a declared one. A module of its own, not named like the
        // module (tsc's resolveESModuleSymbol -> getTypeWithSyntheticDefaultImportType, cloneTypeAsModuleType)
        ret->exports = arena_new(binder_arena(my binder), SymbolTable);
        u32 count;
        SymbolEntry* entries = symbol_collect_exports(module, &count);
        for (u32 i = 0; i < count; i++) {
            if (csview_equals(symbol_entry_name(&entries[i]), "default")) continue;
            symbol_table_insert(ret->exports, binder_arena(my binder), symbol_entry_name(&entries[i]), entries[i].hash, entries[i].symbol);
        }
        free(entries);
        Symbol* synthetic = binder_new_symbol(my binder, c_sv("default"), SYMBOL_ALIAS);
        synthetic->parent = module;
        synthetic->alias = arena_new(binder_arena(my binder), SymbolAlias);
        synthetic->alias->scope = my alias->scope;
        synthetic->alias->target = module;
        synthetic->alias->state = ALIAS_DONE;
        symbol_rare_mut(ret)->synthetic_default = synthetic;
    } else {
        symbol_rare_mut(ret)->gather_module = module;
    }
    symbol_add_decl(ret, (SymbolDecl){
        .kind = SYMBOL_DECL_ALIAS, .flags = SYMBOL_VALUE_MODULE, .node = item,
        .name = item->decl_name, .full = item->full_span,
    });
    return ret;
}

// How tsc names a module in a message: an ambient module by its declared name, a file by the specifier it was
// imported with (TS2305) or by its path without the extension
cstr module_name_for_error(Symbol const* module, ModuleRef const* ref, bool by_specifier) {
    if (!module->module->file) {
        // (An ambient module's symbol name is its name in double quotes whatever the declaration's: tsc's
        // getDeclarationName of a string literal)
        csview name = module->name;
        if (name.size >= 2 && name.buf[0] == '\'' && name.buf[name.size - 1] == '\'') {
            return cstr_d("\"", csview_slice(name, 1, name.size - 1), "\"");
        }
        return cstr_from_sv(name);
    }
    if (by_specifier) return cstr_d("\"", ref->path, "\"");
    cstr shown = source_file_message_path(module->module->file);
    csview path = cstr_sv(&shown);
    // (`// @filename: ./a.ts` is the file `a.ts`: tsc's normalized name)
    while (csview_starts_with(path, "./")) path = csview_slice(path, 2, path.size);
    static const char* const extensions[] = { ".d.ts", ".d.mts", ".d.cts", ".ts", ".tsx", ".js", ".jsx", ".mts", ".cts", ".mjs", ".cjs", ".json" };
    for (size_t i = 0; i < sizeof(extensions) / sizeof(*extensions); i++) {
        if (csview_ends_with(path, extensions[i])) {
            path.size -= (isize)strlen(extensions[i]);
            break;
        }
    }
    cstr ret = cstr_d("\"", path, "\"");
    cstr_drop(&shown);
    return ret;
}

// tsc's getFullyQualifiedName: the containers too, a file module as its quoted name (`"file".A.B`)
cstr symbol_fully_qualified_name(Symbol const* me) {
    if (my is_module && my module) return module_name_for_error(me, nullptr, false);
    // (`import * as ns`'s own namespace symbol, see make_gather_target(), is the module in tsc)
    Symbol const* gathered = symbol_rare(me)->gather_module;
    if (gathered && gathered->is_module && gathered->module) return module_name_for_error(gathered, nullptr, false);
    if (!my parent || (my parent->is_module && !my parent->module)) return cstr_from_sv(my name);
    // (A module's local isn't a member of it: tsc's symbol.parent is only set for exports)
    if (my parent->is_module && my parent->exports && !my is_default_export && !csview_equals(my name, "export=") &&
        symbol_table_get(my parent->exports, my name) != me
    ) {
        return cstr_from_sv(my name);
    }
    cstr ret = symbol_fully_qualified_name(my parent);
    cstr_append_d(&ret, DEFAULT_DISPLAY, ".", my name);
    return ret;
}

// `export { string }`, `export { undefined }`: what tsc's resolveName finds no symbol for but still reports as
// global (checkAndReportErrorForExportingPrimitiveType, undefinedSymbol)
static bool export_names_global_primitive(csview name) {
    return csview_equals(name, "undefined") || is_primitive_type_name(name);
}

bool is_primitive_type_name(csview name) {
    static char const* const names[] = { "any", "string", "number", "boolean", "never", "unknown" };
    for (u32 i = 0; i < c_arraylen(names); i++) {
        if (csview_equals(name, names[i])) return true;
    }
    return false;
}

// tsc's checkExportSpecifier: globalThis or a symbol first declared at the top level of a script (a global that isn't
// from a `declare global` block)
static bool is_global_declaration(Binder const* binder, Symbol const* symbol) {
    if (symbol == binder->global_this || binder_redirect(binder, symbol) == binder->global_this) return true;
    if (!symbol->decl_count || symbol->parent) return false;
    Symbol const* global = symbol_table_get(&binder->globals, symbol->name);
    if (!global || (global != symbol && global != binder_redirect(binder, symbol))) return false;
    // (`global {}` can also be in a script's `declare module "m" {}`: @types/node's buffer.buffer.d.ts)
    if (symbol->decls[0].block) return symbol->decls[0].block->kind == SCOPE_SCRIPT;
    SourceFile const* file = span_source(symbol->decls[0].full) ? span_source(symbol->decls[0].full) : span_source(symbol->decls[0].name);
    return file && !file->is_module;
}

static void report_non_local_export(ErrorCtx* errs, Span name) {
    error_ctx_error_d(errs, name, 2661, "Cannot export '", name, "'. Only local declarations can be exported from a module.");
}

// An ES module's import of a file sure to be an ES module: no synthetic default (tsc's canHaveSyntheticDefault with
// getEmitSyntaxForUsageLocation)
// Whether the import is emitted as ES module syntax (tsc's getEmitSyntaxForModuleSpecifierExpression is ESNext)
static bool esm_usage(PortItem const* item) {
    SourceFile const* importer = span_source(item->decl_name);
    if (!importer || item->is_require) return false;
    ModuleKind usage = source_file_emit_module_kind(importer);
    return usage == MODULE_KIND_PRESERVE || (usage >= MODULE_KIND_ES2015 && usage <= MODULE_KIND_ESNEXT);
}

static bool esm_imports_esm(PortItem const* item, SourceFile const* target) {
    return esm_usage(item) && source_file_implied_format_for_emit(target) == MODULE_KIND_ESNEXT;
}

// An ES module's import of a CommonJS module under node16/nodenext: always a synthetic default
static bool esm_imports_cjs(PortItem const* item, SourceFile const* target) {
    return esm_usage(item) && source_file_implied_format_for_emit(target) == MODULE_KIND_COMMONJS;
}

// tsc resolves the members of a module's object type (resolveAnonymousTypeMembers -> getNamedMembers) with
// symbolIsValue() on each export, which resolves every export alias. A module's type is resolved once: a visit while
// it is being resolved sees it as done, but the aliases the first visit still has in flight are circular (TS2303)
static void resolve_module_type_members(Symbol* module) {
    if (!module->is_module || module->module->type_members_resolved) return;
    module->module->type_members_resolved = true;
    u32 count = 0;
    SymbolEntry* entries = symbol_collect_exports(module, &count);
    for (u32 i = 0; i < count; i++) symbol_resolved_flags(entries[i].symbol);
    free(entries);
}

// tsc's resolveESModuleSymbol for `import * as ns` (esModuleInterop is always on): it looks for call and construct
// signatures on the module's type, resolving its members
static bool namespace_import_resolves_type_members(PortItem const* item) {
    // (Not `import x = require("m")`: tsc's getTargetOfImportEqualsDeclaration only resolveExternalModuleSymbol's it)
    return item->tag == PORT_IMPORT_GATHER && !item->is_require;
}

// tsc's resolveESModuleSymbol: an ES import of a module with an `export =` resolves it (6.0's TS2497, an `export =`
// of neither a namespace nor a variable, is gone: esModuleInterop is always on)
static void check_es_module_symbol(PortItem const* item, Symbol const* module, Symbol* assigned) {
    // (An `export =` that resolves to nothing is tsc's unknownSymbol: neither a module nor a variable)
    if (!assigned || (assigned->is_placeholder && !assigned->is_unresolved) || !item->module_ref) return;
    // (`module.exports = v` of a value that isn't a name is a ValueModule in tsc's bindModuleExportsAssignment)
    Symbol const* export_equals = symbol_get_export(module, c_sv("export="));
    if (export_equals && export_equals->decl_count && export_equals->decls[0].kind == SYMBOL_DECL_ASSIGNMENT) return;
    Symbol const* target = symbol_resolve_alias(assigned);
    if (!target || (target->is_placeholder && !target->is_unresolved) || target->is_module) return;
    if (!target->is_placeholder) symbol_resolved_flags(assigned);
}

// tsc's getSuggestedSymbolForNonexistentModule: an export of container with a name close to name's, reported as
// TS2724 (tsc's errorNoModuleMemberSymbol and resolveEntityName). False if there is none
bool scope_report_misspelled_export(Symbol const* container, Span name, csview container_name, bool declared_here, ErrorCtx* errs) {
    u32 const module_member = SYMBOL_VARIABLE | SYMBOL_FUNCTION | SYMBOL_CLASS | SYMBOL_INTERFACE | SYMBOL_ENUM |
        SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE | SYMBOL_TYPE_ALIAS | SYMBOL_ALIAS;
    u32 count = 0;
    SymbolEntry* entries = symbol_collect_exports(container, &count);
    csview* names = malloc(sizeof(csview) * (count + 1));
    panic_if(!names, "out of memory");
    for (u32 i = 0; i < count; i++) {
        Symbol* symbol = entries[i].symbol;
        csview candidate = symbol_entry_name(&entries[i]);
        bool fits = candidate.size && candidate.buf[0] != '"' && !csview_equals(candidate, "export=") &&
            !symbol->is_placeholder && ((symbol->flags & module_member) || (symbol_resolved_flags(symbol) & module_member));
        names[i] = fits ? candidate : (csview){0};
    }
    isize best = spelling_suggestion(port_item_name(name), names, count);
    free(names);
    if (best >= 0) {
        csview suggestion = symbol_entry_name(&entries[best]);
        Error* error = error_ctx_error_d(errs, name, 2724, "'", container_name, "' has no exported member named '",
            name, "'. Did you mean '", suggestion, "'?");
        // (Its valueDeclaration, which an interface, a type alias or an alias (`export { X }`) lacks. Only an import's:
        // tsc's resolveEntityName has no related information)
        Symbol const* symbol = entries[best].symbol;
        for (u32 i = 0; error && declared_here && i < symbol->decl_count; i++) {
            if (!(symbol->decls[i].flags & SYMBOL_VALUE) || !span_valid(symbol->decls[i].name)) continue;
            error_add_related_d(error, symbol->decls[i].name, 2728, "'", suggestion, "' is declared here.");
            break;
        }
    }
    free(entries);
    return best >= 0;
}

void scope_report_non_exported_member(PortItem const* item, Symbol const* module, ErrorCtx* errs) {
    Span name = item->source_name;
    // (tsc's errorNoModuleMemberSymbol: a close enough export (for an identifier), then a default export)
    if (span_valid(name) && name.size && name.buf[0] != '"' && name.buf[0] != '\'') {
        cstr module_name = module_name_for_error(module, item->module_ref, true);
        // (Among the exports of what `export =` names, tsc's targetSymbol)
        Symbol const* target = module_export_equals((Symbol*)module);
        if (target != module) target = target && !target->is_placeholder ? symbol_for_meaning(target, SYMBOL_NAMESPACE) : nullptr;
        bool reported = target && scope_report_misspelled_export(target, name, cstr_sv(&module_name), true, errs);
        if (!reported && symbol_get_export(module, c_sv("default"))) {
            error_ctx_error_d(errs, name, 2614, "Module '", module_name, "' has no exported member '", name,
                "'. Did you mean to use 'import ", name, " from ", module_name, "' instead?");
            reported = true;
        }
        cstr_drop(&module_name);
        if (reported) return;
    }
    cstr module_name = module_name_for_error(module, item->module_ref, true);
    SourceFile const* file = module->is_module && module->module ? module->module->file : nullptr;
    Symbol* local = file && file->scope ? symbol_table_get(scope_locals(file->scope), port_item_name(name)) : nullptr;
    Symbol const* export_equals = local ? symbol_get_export(module, c_sv("export=")) : nullptr;
    if (!local || (export_equals && symbol_resolve_alias((Symbol*)export_equals) != symbol_resolve_alias(local))) {
        error_ctx_error_d(errs, name, 2305, "Module '", module_name, "' has no exported member '", name, "'.");
    } else if (export_equals) {
        // (tsc's reportInvalidImportEqualsExportMember)
        CompilerOptions const* options = compiler_options();
        if (options->module >= MODULE_KIND_ES2015) {
            error_ctx_error_d(errs, name, 2595, "'", name, "' can only be imported by using a default import.");
        } else if (span_source(name) && span_source(name)->is_js) {
            error_ctx_error_d(errs, name, 2597, "'", name, "' can only be imported by using a 'require' call or by using a default import.");
        } else {
            error_ctx_error_d(errs, name, 2616, "'", name, "' can only be imported by using 'import ", name, " = require(",
                module_name, ")' or a default import.");
        }
    } else {
        // Declared but not exported, or exported under another name
        Symbol const* resolved = symbol_resolve_alias(local);
        SymbolTable const* exports = symbol_exports_table(module);
        csview exported_as = {0};
        // (tsc's getSymbolIfSameReference: whatever doesn't resolve is unknownSymbol, one symbol, so an unresolved import
        // is "exported as" the first unresolved export)
        bool unresolved = resolved && resolved->is_unresolved;
        for (u32 i = 0; exports && i < exports->count && !exported_as.size; i++) {
            Symbol const* other = symbol_resolve_alias(exports->entries[i].symbol);
            if (other == resolved || (unresolved && other && other->is_unresolved)) {
                exported_as = symbol_entry_name(&exports->entries[i]);
            }
        }
        Error* error = exported_as.size ?
            error_ctx_error_d(errs, name, 2460, "Module '", module_name, "' declares '", name,
                "' locally, but it is exported as '", exported_as, "'.") :
            error_ctx_error_d(errs, name, 2459, "Module '", module_name, "' declares '", name, "' locally, but it is not exported.");
        for (u32 i = 0; i < local->decl_count; i++) {
            if (i == 0) error_add_related_d(error, local->decls[i].name, 2728, "'", name, "' is declared here.");
            else error_add_related_d(error, local->decls[i].name, 6204, "and here.");
        }
    }
    cstr_drop(&module_name);
}

// TS1259: a default import of an `export =` module without a synthetic default (tsc's getTargetofModuleDefault)
void scope_report_default_import_of_export_equals(PortItem const* item, Symbol const* module, ErrorCtx* errs) {
    Symbol const* export_equals = symbol_get_export((Symbol*)module, c_sv("export="));
    const char* option = compiler_options()->module >= MODULE_KIND_ES2015 ? "allowSyntheticDefaultImports" : "esModuleInterop";
    cstr module_name = module_name_for_error(module, item->module_ref, false);
    Error* error = error_ctx_error_d(errs, item->decl_name, 1259, "Module '", module_name,
        "' can only be default-imported using the '", option, "' flag");
    if (error && export_equals && export_equals->decl_count) {
        error_add_related_d(error, export_equals->decls[0].full, 2594,
            "This module is declared with 'export =', and can only be used with a default import when "
            "using the '", option, "' flag.");
    }
    cstr_drop(&module_name);
}

// tsc's canHaveSyntheticDefault: whether a default import of `module` is the module itself (what `export =` names)
static bool can_have_synthetic_default(PortItem const* item, Symbol* module, SourceFile const* file) {
    ModuleKind module_kind = compiler_options()->module;
    if (file) {
        // In Node.js a CommonJS module imported into an ES module always has a synthetic default; when both are sure to
        // be ES modules there is none, whatever `module` says
        if (module_kind >= MODULE_KIND_NODE16 && module_kind <= MODULE_KIND_NODENEXT && esm_imports_cjs(item, file)) return true;
        if (esm_imports_esm(item, file)) return false;
        // A referenced project's file, without a format of its own: its project's
        if (file->reference_emits_esm && source_file_implied_format_for_emit(file) == MODULE_KIND_NONE &&
            esm_usage(item)) {
            return false;
        }
    }
    if (!file || source_file_reads_as_declaration(file)) {
        // Declaration files and ambient modules: unless they say they have a default or are ES modules (`__esModule`).
        // (Every default export of a declaration file is tsc's isSyntacticDefault)
        if (symbol_get_export(module, c_sv("default")) || symbol_get_export(module, c_sv("__esModule"))) return false;
        // (With `export =`, tsc's resolveExportByName looks among what it assigns: a namespace's `export { x as
        // default }` is a syntactic default, a class's static `default` isn't)
        Symbol* assigned = module->is_module ? module_export_equals(module) : nullptr;
        if (assigned && assigned != module && !assigned->is_placeholder) {
            Symbol* found = symbol_get_export(assigned, c_sv("default"));
            if (found && (found->flags & SYMBOL_ALIAS || found->is_default_export)) return false;
            if (symbol_get_export(assigned, c_sv("__esModule"))) return false;
        }
        return true;
    }
    // TypeScript files are emitted with an `__esModule` marker: only `export =` makes one
    if (!file->is_js) return symbol_get_export(module, c_sv("export=")) != nullptr;
    // JavaScript files without ES module syntax nor an `__esModule` marker
    return !span_valid(file->module_indicator) && !symbol_get_export(module, c_sv("__esModule"));
}

// tsc's reportNonDefaultExport: TS2613 when the module exports what the import names, TS1192 otherwise
static void report_non_default_export(PortItem const* item, Symbol* module, ErrorCtx* errs) {
    cstr module_name = module_name_for_error(module, item->module_ref, false);
    if (symbol_get_export(module, span_sv(item->decl_name))) {
        error_ctx_error_d(errs, item->decl_name, 2613, "Module '", module_name,
            "' has no default export. Did you mean to use 'import { ", span_sv(item->decl_name), " } from ", module_name,
            "' instead?");
        cstr_drop(&module_name);
        return;
    }
    Error* error = error_ctx_error_d(errs, item->decl_name, 1192, "Module '", module_name, "' has no default export.");
    // (the first `export *` of a module that has a default)
    for (u32 s = 0; error && module->is_module && s < module->module->export_star_count; s++) {
        PortItem const* star = module->module->export_stars[s];
        Symbol const* from = binder_module_of_ref(module->binder, star->module_ref);
        if (from && from->exports && symbol_table_get(from->exports, c_sv("default"))) {
            error_add_related_d(error, span_with_semicolon(star->full_span), 1195,
                "'export *' does not re-export a default.");
            break;
        }
    }
    cstr_drop(&module_name);
}

// An ES module's `export { x as "module.exports" }`, what Node.js's require() of it returns (node20..nodenext)
static Symbol* module_dot_exports(Symbol* module) {
    ModuleKind module_kind = compiler_options()->module;
    if (module_kind < MODULE_KIND_NODE20 || module_kind > MODULE_KIND_NODENEXT || !module->is_module) return nullptr;
    return symbol_get_export(module, c_sv("module.exports"));
}

// A CommonJS import (by emit syntax: tsc's getEmitSyntaxForModuleSpecifierExpression) of an ES module file
static bool cjs_imports_esm(PortItem const* item, Symbol const* module) {
    SourceFile const* importer = span_source(item->decl_name);
    SourceFile const* target = module->is_module ? module->module->file : nullptr;
    if (!importer || !target || item->is_require) return false;
    return source_file_emit_module_kind(importer) == MODULE_KIND_COMMONJS &&
        source_file_implied_format_for_emit(target) == MODULE_KIND_ESNEXT;
}

// tsc's getTargetofModuleDefault: `import d from "m"`, `import { default as d }`, `export { default } from "m"`
static Symbol* resolve_default_target(PortItem* item, Symbol* module, Symbol* assigned, ErrorCtx* errs) {
    SourceFile const* file = module->is_module ? module->module->file : nullptr;
    Symbol* found = symbol_get_export(module, c_sv("default"));
    // In Node.js an ES module's import of JSON (a .json file or a .d.json.ts declaration) has only a default, the
    // module itself (tsc's isOnlyImportableAsDefault in getTargetOfImportClause)
    ModuleKind module_kind = compiler_options()->module;
    SourceFile const* importer = span_source(item->decl_name);
    // A CommonJS default import of an ES module with a `module.exports` export: `__importDefault(require())` with
    // esModuleInterop, broken without
    Symbol* dot_exports = cjs_imports_esm(item, module) ? module_dot_exports(module) : nullptr;
    if (dot_exports) {
        item->source_symbol = dot_exports;
        return resolve_if_pure_alias(dot_exports);
    }
    bool default_only = file && importer && module_kind >= MODULE_KIND_NODE16 && module_kind <= MODULE_KIND_NODENEXT &&
        !item->is_require && source_file_emit_module_kind(importer) == MODULE_KIND_ESNEXT &&
        (file->is_json || csview_ends_with(cstr_sv(&file->canonical_path), ".d.json.ts"));
    if (default_only || can_have_synthetic_default(item, module, file)) {
        // A synthetic default overrides a real `default` export (as emitted without `__esModule`). (`{ default as d }`
        // names the `export=` symbol itself)
        if (assigned && item->tag != PORT_IMPORT_DEFAULT) item->source_symbol = symbol_get_export(module, c_sv("export="));
        return assigned ? assigned : module;
    }
    if (!found) {
        Symbol const* export_equals = symbol_get_export(module, c_sv("export="));
        // (What it assigns exports a syntactic default itself, `namespace a { export { x as default } }`: tsc's
        // resolveExportByName finds it among the assigned type's properties, see can_have_synthetic_default())
        Symbol* assigned_default = export_equals && assigned &&
            assigned != module && !assigned->is_placeholder ? symbol_get_export(assigned, c_sv("default")) : nullptr;
        if (assigned_default) {
            item->source_symbol = assigned_default;
            return resolve_if_pure_alias(assigned_default);
        }
        if (item->tag == PORT_IMPORT_DEFAULT) {
            report_non_default_export(item, module, errs);
        } else {
            scope_report_non_exported_member(item, module, errs); // (tsc's errorNoModuleMemberSymbol)
        }
        return nullptr;
    }
    item->source_symbol = found;
    return resolve_if_pure_alias(found);
}

// `export = a.b.c`: the value the expression is, a symbol of its own like a non-alias `export =` (what tsc gets from
// checking the expression when resolveEntityName finds nothing: the property)
static Symbol* entity_value_symbol(Symbol* me) {
    Symbol* ret = binder_new_symbol(my binder, my name, SYMBOL_BLOCK_SCOPED_VARIABLE);
    ret->name_span = my name_span;
    ret->parent = my parent;
    ret->is_default_export = my is_default_export;
    Span statement = my decls[0].full;
    symbol_add_decl(ret, (SymbolDecl){
        .kind = SYMBOL_DECL_EXPR, .flags = SYMBOL_BLOCK_SCOPED_VARIABLE, .node = my alias->entity,
        .name = statement, .full = statement,
    });
    return ret;
}

// tsc's resolveEntityName(`a.b.c`, Value | Type | Namespace): each name but the last a namespace, the last anything.
// A class's static members are its exports in tsc, merged with a namespace's: a static member of the class the last
// name is looked up in is the value half (value_target) of what the namespace exports under that name
static Symbol* resolve_entity_alias(Symbol* me) {
    Expr const* names[32];
    u32 count = 0;
    Expr const* e = my alias->entity;
    for (; e->tag == EXPR_PROPERTY && count < c_arraylen(names); e = &e->property->expr) names[count++] = e;
    if (e->tag != EXPR_IDENTIFIER) return entity_value_symbol(me);
    Symbol* target = scope_resolve(my alias->scope, span_sv(e->inner_span), SYMBOL_NAMESPACE);
    if (!target || target == me) return entity_value_symbol(me);
    target = resolve_if_pure_alias(target);
    for (u32 i = count; i-- > 0;) {
        csview name = span_sv(property_key_inner_span(names[i]->property));
        Symbol const* container = symbol_for_meaning(target, SYMBOL_NAMESPACE);
        if (!container || container->is_placeholder) return entity_value_symbol(me);
        Symbol* found = symbol_get_export(container, name);
        Symbol* next = resolve_if_pure_alias(found);
        if (!next && !(container->flags & SYMBOL_VALUE & ~(u32)SYMBOL_VALUE_MODULE)) {
            // A namespace's value has its exports as properties and nothing else: checking the expression finds no
            // symbol (and says so), what tsc's resolveAlias takes as unknownSymbol, silent from there on
            Symbol* unknown = binder_new_placeholder(my binder, my name_span, my name_span);
            unknown->is_unresolved = true;
            return unknown;
        }
        if (!next || next == me) return entity_value_symbol(me);
        if (i > 0 && !(symbol_resolved_flags(next) & SYMBOL_NAMESPACE) && !next->is_placeholder) {
            return entity_value_symbol(me);
        }
        if (i == 0 && !(symbol_resolved_flags(next) & SYMBOL_VALUE) && container->flags & SYMBOL_CLASS) {
            ClassDecl const* class_decl = symbol_find_node(container, SYMBOL_DECL_CLASS);
            if (class_decl && class_decl_find_member(class_decl, name, true)) my alias->value_target = entity_value_symbol(me);
        }
        target = next;
    }
    return target;
}

typedef struct Suggestion {
    csview name;
    Symbol const* symbol; // Null for a primitive (`string` for `String`)
    // An export of the module or namespace suggested as a value: tsc finds its local (SymbolFlags.ExportValue), which
    // has no value declaration to relate to
    bool export_local;
} Suggestion;

static bool scope_suggest(Scope const* me, csview name, u32 meaning, Suggestion* out);

static Symbol* resolve_alias_target(Symbol* me) {
    PortItem* item = my alias->item;
    ErrorCtx* errs = item && (item->wrong_context || item->clause_unchecked) ? nullptr : my alias->scope->binder->errs;
    if (my alias->class_expr) return my alias->class_expr->symbol;
    if (my alias->entity) return resolve_entity_alias(me);
    if (!item) {
        // `export default x`, TS2304 comes from the expression
        Symbol* found = scope_resolve(my alias->scope, span_sv(my alias->name), SYMBOL_ANY_MEANING);
        // (A predefined type keyword isn't a symbol to tsc's resolveEntityName: `export = string` names nothing, the
        // unknown symbol, which has every meaning)
        if (symbol_is_builtin_type(found)) found = nullptr;
        SourceFile const* file = my alias->scope->file;
        if (found == me) return nullptr;
        // tsc's resolveNameHelper looks a CommonJS module's exports up after its locals and before the globals, and
        // discards what it finds (but typedefs); getSymbol's meaning test resolves a pure alias there all the same, so
        // `exports.y = y` or `module.exports = { parseInt }` with no local of the name is its own target in flight:
        // circular (TS2303), the global or TS2304 aside
        if (file && file->is_js && file->is_commonjs && file->scope && file->scope->container &&
            file->scope->container->exports && (!found || found == symbol_table_get(&my binder->globals, span_sv(my alias->name)))
        ) {
            Symbol* exported = symbol_table_get(file->scope->container->exports, span_sv(my alias->name));
            if (exported && (exported->flags & SYMBOL_ALIAS) && !(exported->flags & (SYMBOL_VALUE | SYMBOL_TYPE | SYMBOL_NAMESPACE))) {
                symbol_resolve_alias(exported);
                if (my alias->state == ALIAS_CIRCULAR) return nullptr;
            }
        }
        if (!found && span_valid(my alias->name) && !binder_shard(my binder)) {
            // (tsc's resolveEntityName reports TS2304 through resolveName, whose spelling suggestion resolves the
            // aliases among the candidates: one that reaches this alias in flight makes it TS2303. The TS2304 itself
            // comes from the expression)
            Suggestion ignored = {0};
            scope_suggest(my alias->scope, span_sv(my alias->name), SYMBOL_VALUE | SYMBOL_TYPE | SYMBOL_NAMESPACE, &ignored);
        }
        // A local type and an exported value of the name are one symbol in tsc, the local one, which has the exported
        // one as its exportSymbol: `interface F {}` + `export const F = ...` + `export default F` exports that local
        // symbol, whose value meaning is only ExportValue: used as a value, the default is tsc's errorType, silently
        // (getTypeOfAlias). A placeholder value_target (the error type) says so
        Symbol* container = my alias->scope->container;
        if (found && !(found->flags & SYMBOL_VALUE) && container && container->exports) {
            Symbol* value = symbol_table_get(container->exports, span_sv(my alias->name));
            if (value && value != found && value != me && (value->flags & SYMBOL_VALUE)) {
                Symbol* unknown = binder_new_placeholder(my binder, my alias->name, my alias->name);
                unknown->is_unresolved = true;
                unknown->flags |= SYMBOL_VALUE & value->flags;
                my alias->value_target = unknown;
            }
        }
        unused_mark_referenced(my alias->scope, found, SYMBOL_ANY_MEANING);
        return resolve_if_pure_alias(found);
    }
    if (item->source_phase) {
        // `import source s from "m"`: no module, a variable of the module's source type (tsgo's
        // getTargetOfImportClause). Named and namespace imports (TS18112) are of nothing, any
        if (item->tag != PORT_IMPORT_DEFAULT) return nullptr;
        Symbol* target = binder_new_symbol(my binder, my name, SYMBOL_FUNCTION_SCOPED_VARIABLE);
        target->name_span = item->decl_name;
        symbol_add_decl(target, (SymbolDecl){ .kind = SYMBOL_DECL_SOURCE_PHASE, .flags = SYMBOL_FUNCTION_SCOPED_VARIABLE,
            .node = item, .name = item->decl_name, .full = item->full_span });
        return target;
    }
    Symbol* module = nullptr;
    Symbol* assigned = nullptr; // What its `export =` names, if it has one
    if (item->tag != PORT_IMPORT_ALIAS && item->tag != PORT_EXPORT_SPECIFIC && item->tag != PORT_EXPORT_AS_NAMESPACE) {
        module = binder_module_of_ref(my binder, item->module_ref);
        if (!module) {
            // No file: TS2307 comes from the statement. (An `@import` tag too: tsgo's reparser makes it an import
            // declaration, whose checkImportBinding resolves it)
            if (item->module_ref->file) {
                cstr shown = module_ref_file_message_path(item->module_ref);
                error_ctx_error_d(errs, item->module_ref->span, 2306, "File '", shown, "' is not a module.");
                cstr_drop(&shown);
            }
            return nullptr;
        }
        // Everything is any. But a default import looks for a `default` export first, which a non-shorthand declaration
        // merged with it may have (tsgo's getTargetOfModuleDefault doesn't short-circuit shorthand modules: `declare
        // module '*.css';` and another file's `declare module '*.css' { export default s }` give s's type)
        if (module->is_shorthand_module && !(item->tag == PORT_IMPORT_DEFAULT && symbol_get_export(module, c_sv("default")))) {
            return nullptr;
        }
        assigned = module_export_equals(module);
        if (assigned == module) assigned = nullptr;
    }
    switch (item->tag) {
        case PORT_IMPORT_SPECIFIC:
        case PORT_REEXPORT_SPECIFIC: {
            if (!item->source_name.size) return nullptr; // A missing name (`import { "x" }`, the parser's TS1003)
            if (csview_equals(port_item_name(item->source_name), "default")) {
                return resolve_default_target(item, module, assigned, errs);
            }
            // (tsc's getExternalModuleMember: `{ default as x }` with allowSyntheticDefaultImports is fine)
            if (!csview_equals(port_item_name(item->source_name), "default")) {
                check_es_module_symbol(item, module, assigned);
            }
            // (An `export =` of nothing is tsc's unknownSymbol, whose exports are all there is: not the module's
            // other exports)
            Symbol* found = assigned && assigned->is_placeholder && assigned->is_unresolved ? nullptr :
                symbol_get_export(module, port_item_name(item->source_name));
            // A module's `export=` hides its other exports, but for its types (tsgo's getExportsOfModuleWorker: those
            // without a value meaning; bind_type_exports() moves them to the `export=`). A CommonJS module's typedefs'
            // value half is the assigned value's property
            bool commonjs = assigned && module->is_module && module->module && module->module->file &&
                module->module->file->is_commonjs;
            if (assigned && found && (symbol_resolved_flags(found) & SYMBOL_VALUE)) found = nullptr;
            if (assigned && (!found || (commonjs && !(symbol_resolved_flags(found) & SYMBOL_VALUE)))) {
                if (assigned->is_placeholder) {
                    // (tsc's unknownSymbol has no members)
                    if (assigned->is_unresolved) scope_report_non_exported_member(item, module, errs);
                    return nullptr;
                }
                Symbol const* container = found ? nullptr : symbol_for_meaning(assigned, SYMBOL_NAMESPACE);
                if (container) found = symbol_get_export(container, port_item_name(item->source_name));
                // The properties of the value `export =` names are importable too (tsc's getExternalModuleMember).
                // Whether it has the property is up to the checker: without one the value is the error type
                bool from_value = symbol_resolved_flags(assigned) & SYMBOL_VALUE &&
                    !csview_equals(port_item_name(item->source_name), "default");
                // A function (with a namespace) has no properties but the namespace's values: a type the namespace
                // exports isn't a value too (tsc's getPropertyOfType of the function's type finds nothing, TS2693)
                u32 value_flags = symbol_resolved_flags(assigned) & SYMBOL_VALUE;
                if (from_value && found && (value_flags & ~(u32)(SYMBOL_FUNCTION | SYMBOL_VALUE_MODULE)) == 0) from_value = false;
                // (A namespace's only properties are its value exports, which the binder knows: one without the name
                // is tsc's unknownSymbol for it, TS2305)
                if (from_value && !found && value_flags == SYMBOL_VALUE_MODULE && !(symbol_resolved_flags(assigned) & SYMBOL_ALIAS)) {
                    from_value = false;
                }
                if (from_value && !(found && symbol_resolved_flags(found) & SYMBOL_VALUE)) {
                    Symbol* property = binder_new_symbol(my binder, port_item_name(item->source_name), SYMBOL_PROPERTY);
                    property->name_span = item->source_name;
                    symbol_add_decl(property, (SymbolDecl){
                        .kind = SYMBOL_DECL_EXPORT_EQUALS_PROPERTY,
                        .flags = SYMBOL_PROPERTY,
                        .node = assigned,
                        .name = item->source_name,
                        .full = item->full_span,
                    });
                    if (!found) return property;
                    my alias->value_target = property;
                    Symbol* type = (Symbol*)resolve_if_pure_alias(found);
                    if (type && (type->flags & SYMBOL_TYPE) == SYMBOL_INTERFACE && !(type->flags & (SYMBOL_VALUE | SYMBOL_NAMESPACE))) {
                        Symbol* combined = binder_new_symbol(my binder, type->name, SYMBOL_INTERFACE);
                        combined->name_span = type->name_span;
                        for (u32 i = 0; i < type->decl_count; i++) symbol_add_decl(combined, type->decls[i]);
                        my alias->combined_type = combined;
                    }
                }
                // (Neither the namespace nor the value has it: TS2305 below)
            }
            if (!found) {
                scope_report_non_exported_member(item, module, errs);
                return nullptr;
            }
            item->source_symbol = found;
            my alias->type_only_star = module_type_only_star(module, port_item_name(item->source_name));
            return resolve_if_pure_alias(found);
        }

        case PORT_IMPORT_DEFAULT:
            return resolve_default_target(item, module, assigned, errs);

        case PORT_EXPORT_SPECIFIC: {
            Symbol* found = scope_resolve(my alias->scope, span_sv(item->source_name), SYMBOL_ANY_MEANING);
            // (tsc's primitive types and `undefined` aren't symbols its resolveName finds)
            if (symbol_is_builtin_type(found)) found = nullptr;
            if (found == me && (my flags & SYMBOL_ANY_MEANING & ~(u32)SYMBOL_ALIAS)) {
                // `export class F {}` + `export { F }`: one export symbol (TS2323), which tsc's resolveName finds for
                // the specifier and is its own target, conflicting with itself in checkAliasSymbol. In a JavaScript
                // file a type target (a JSDoc typedef the file exports by itself) is TS18043 there first
                if (span_source(item->source_name) && span_source(item->source_name)->is_js && !(my flags & SYMBOL_VALUE) &&
                    !item->type_only
                ) {
                    Error* error = error_ctx_error_d(errs, item->source_name, 18043,
                        "Types cannot appear in export declarations in JavaScript files.");
                    for (u32 i = 0; i < my decl_count; i++) {
                        if (my decls[i].kind != SYMBOL_DECL_TYPE_ALIAS || !((TypeDecl const*)my decls[i].node)->jsdoc) continue;
                        error_add_related_d(error, span_valid(my decls[i].name) ? my decls[i].name : my decls[i].full, 18044, "'", item->source_name, "' is automatically exported here.");
                        break;
                    }
                    return nullptr;
                }
                error_ctx_error_d(errs, item->full_span, 2484,
                    "Export declaration conflicts with exported declaration of '", item->decl_name, "'.");
                // (Its own target: tsc's getSymbolFlags then stops at it, a type alias is only a type)
                return me;
            }
            if (!found || found == me) {
                if (export_names_global_primitive(span_sv(item->source_name))) {
                    report_non_local_export(errs, item->source_name);
                } else if (item->source_name.size && (*item->source_name.buf == '"' || *item->source_name.buf == '\'')) {
                    // `export { "x" }` (tsc's checkExportSpecifier skips it: the parser's TS1003)
                } else {
                    // (tsc's resolveName for the export specifier: with spelling suggestions)
                    scope_report_not_found(my alias->scope, item->source_name, SYMBOL_ANY_MEANING, false, errs);
                }
                return nullptr;
            }
            if (is_global_declaration(my binder, found)) report_non_local_export(errs, item->source_name);
            unused_mark_referenced(my alias->scope, found, SYMBOL_ANY_MEANING);
            item->source_symbol = found;
            Symbol* target = resolve_if_pure_alias(found);
            // An `import x = A.B` that resolves to nothing is exported as a value (tsc's checkExportSpecifier checks the
            // name as an expression when the target is unknown, which checks A as a value: markAliasReferenced)
            if ((!target || target->is_placeholder) && (found->flags & SYMBOL_ALIAS) && found->alias && found->alias->item &&
                found->alias->item->tag == PORT_IMPORT_ALIAS && !found->alias->item->value_use_checked &&
                vec_span_size(&port_item_alias(found->alias->item)->path) && found->alias->scope
            ) {
                PortItem* alias_item = found->alias->item;
                alias_item->value_use_checked = true;
                Span first = *vec_span_front(&port_item_alias(alias_item)->path);
                if (first.size && !scope_get_value(found->alias->scope, span_sv(first), nullptr)) {
                    scope_report_value_not_found(found->alias->scope, first, false, found->alias->scope->binder->errs);
                }
            }
            return target;
        }

        case PORT_IMPORT_GATHER:
        case PORT_REEXPORT_GATHER: {
            // Node.js's require() of an ES module is its `export { x as "module.exports" }` if it has one: for
            // `import x = require()` and JS's `require()` under node20..nodenext (tsc's
            // getTargetOfImportEqualsDeclaration), for a CommonJS file's `import * as ns` of an ES module (tsc's
            // resolveESModuleSymbol)
            if (item->tag == PORT_IMPORT_GATHER && (item->is_require || cjs_imports_esm(item, module))) {
                Symbol* dot_exports = module_dot_exports(assigned ? assigned : module);
                if (dot_exports) {
                    item->source_symbol = dot_exports;
                    return resolve_if_pure_alias(dot_exports);
                }
            }
            if (!item->is_require) check_es_module_symbol(item, module, assigned);
            if (assigned) return assigned;
            if (namespace_import_resolves_type_members(item)) resolve_module_type_members(module);
            return make_gather_target(me, module);
        }

        case PORT_IMPORT_ALIAS:
            return resolve_import_alias_path(me);

        case PORT_EXPORT_AS_NAMESPACE: {
            Symbol* own_module = my alias->scope->container;
            Symbol* own_assigned = module_export_equals(own_module);
            return own_assigned != own_module ? own_assigned : make_gather_target(me, own_module);
        }

        case PORT_NOOP:
        case PORT_REEXPORT_SPLAT:
            break;
    }
    impossible;
}

// The aliases being resolved, innermost last (tsc's resolution stack, pushTypeResolution)
static thread_local Symbol** alias_stack;
static thread_local u32 alias_stack_count, alias_stack_cap;

void scope_release_thread(void) {
    free(alias_stack);
    alias_stack = nullptr;
    alias_stack_count = alias_stack_cap = 0;
}

Symbol* symbol_resolve_alias(Symbol* me) {
    if (!me || !(my flags & SYMBOL_ALIAS)) return me;
    switch (my alias->state) {
        case ALIAS_DONE: return my alias->target;
        case ALIAS_IN_FLIGHT:
        case ALIAS_CIRCULAR:
            // (tsc's resolveAlias: the inner visit is unknown, and every alias of the cycle, from this one to the
            // innermost, reports when it is done: findResolutionCycleStartIndex)
            for (u32 i = alias_stack_count; i-- > 0;) {
                alias_stack[i]->alias->state = ALIAS_CIRCULAR;
                if (alias_stack[i] == me) break;
            }
            my alias->state = ALIAS_CIRCULAR;
            return nullptr;
        case ALIAS_UNRESOLVED: break;
    }
    if (my binder->frozen) log_error("Alias '", my name, "' resolved while typechecking: the checkers share it");
    my alias->state = ALIAS_IN_FLIGHT;
    *(Symbol**)list_push((void**)&alias_stack, &alias_stack_count, &alias_stack_cap, sizeof(Symbol*)) = me;
    Symbol* target = resolve_alias_target(me);
    alias_stack_count--;
    // (`export default x` looks its name up in a scope that has it)
    if (my alias->state == ALIAS_CIRCULAR && (my alias->item || (my decl_count && span_valid(my decls[0].full)))) {
        // (Import-equals declarations and `export =` are reported on the whole statement)
        PortItem const* item = my alias->item;
        // (A CommonJS export's at its name: `exports.y`, the shorthand)
        SourceFile const* file = my alias->scope->file;
        bool commonjs = !item && file && file->is_js && file->is_commonjs && span_valid(my decls[0].name);
        Span at = commonjs ? my decls[0].name : !item ? my decls[0].full :
            item->tag == PORT_IMPORT_ALIAS || item->is_require ? span_with_semicolon(item->full_span) : item->decl_name;
        // (A specifier is reported whole: `A as B`)
        bool specifier = item && (item->tag == PORT_IMPORT_SPECIFIC || item->tag == PORT_EXPORT_SPECIFIC ||
            item->tag == PORT_REEXPORT_SPECIFIC);
        if (specifier && span_valid(item->source_name) && span_source(item->source_name) == span_source(at) &&
            span_start_ptr(item->source_name) < span_start_ptr(at)
        ) {
            at = span_make_in(at, span_start_ptr(item->source_name), span_end_ptr(at));
        }
        if (!(item && item->wrong_context)) {
            error_ctx_error_d(my alias->scope->binder->errs, at, 2303, "Circular definition of import alias '", my name, "'.");
        }
        // (tsc's resolveAlias: the inner visit made the target unknownSymbol, and it stays that)
        target = nullptr;
    }
    if (!target) {
        target = my alias->item ?
            binder_new_placeholder(my binder, my alias->item->decl_name, my alias->item->full_span) :
            binder_new_placeholder(my binder, my alias->name, my alias->name);
        ModuleRef* ref = my alias->item ? my alias->item->module_ref : nullptr;
        Symbol const* module = ref ? binder_module_of_ref(my binder, ref) : nullptr;
        target->is_unresolved = !(module && module->is_shorthand_module);
    }
    my alias->target = target;
    my alias->state = ALIAS_DONE;
    return target;
}

#define ALIAS_CHAIN_MAX 32

static u32 resolved_flags(Symbol const* me, int depth) {
    if (!me) return 0;
    u32 flags = my flags & ~(u32)SYMBOL_ALIAS;
    if (my flags & SYMBOL_ALIAS && depth < ALIAS_CHAIN_MAX) {
        Symbol const* target = symbol_resolve_alias((Symbol*)me);
        // An alias still being resolved (a circular one) has every meaning, like tsc's unknownSymbol in getSymbolFlags
        // (one that is its own target has its own: tsc's `target === symbol` stop)
        if (target != me) flags |= target ? resolved_flags(target, depth + 1) : SYMBOL_ANY_MEANING;
        if (my alias->value_target) flags |= my alias->value_target->flags;
    }
    return flags;
}

u32 symbol_resolved_flags(Symbol const* me) {
    return resolved_flags(me, 0);
}

Symbol const* symbol_namespace_member(Symbol const* me, csview name) {
    Symbol const* member = symbol_get_export((Symbol*)me, name);
    if (member || !me || !(me->flags & SYMBOL_ALIAS)) return member;
    // (tsc's resolveQualifiedName: an alias with a namespace meaning of its own, a CommonJS `export=` its module's
    // typedefs made one, has the members of what it resolves to too)
    Symbol const* resolved = symbol_resolve_alias((Symbol*)me);
    Symbol const* target = resolved && resolved != me ? symbol_for_meaning(resolved, SYMBOL_NAMESPACE) : nullptr;
    // (Or a function or class with expandos, which has exports without a namespace meaning: tsgo's getExportsOfSymbol)
    if (!target && resolved && resolved != me && resolved->exports && symbol_find_decl(resolved, SYMBOL_DECL_EXPANDO)) target = resolved;
    return target && !target->is_placeholder ? symbol_get_export((Symbol*)target, name) : nullptr;
}

Symbol const* symbol_for_meaning(Symbol const* me, u32 meaning) {
    for (int depth = 0; me && depth < ALIAS_CHAIN_MAX; depth++) {
        if (my flags & meaning || my is_placeholder) return me;
        if (!(my flags & SYMBOL_ALIAS)) return nullptr;
        Symbol const* target = symbol_resolve_alias((Symbol*)me);
        if (target && !(target->flags & meaning) && my alias->value_target && my alias->value_target->flags & meaning) {
            return my alias->value_target;
        }
        me = target;
    }
    return nullptr;
}

static PortItem const* type_only_declaration_at(Symbol const* me, int depth, Symbol const** named);

// (A merge with a value along the chain makes it no type-only alias: tsc's getSymbolFlags with
// excludeTypeOnlyMeanings collects the meanings of every alias it passes)
PortItem const* symbol_type_only_declaration(Symbol const* me) {
    return type_only_declaration_at(me, 0, nullptr);
}

// tsc's getTypeOnlyAliasDeclaration, also saying what the declaration names: each alias of the chain is marked by its
// immediate target's while that is a pure alias (tsgo 7.1's resolveIndirectionAlias); one merged with a value ends it
// (typeOnlyMerge3)
PortItem const* symbol_type_only_declaration_naming(Symbol const* me, Symbol const** named) {
    *named = nullptr;
    return type_only_declaration_at(me, 0, named);
}

static PortItem const* type_only_declaration_at(Symbol const* me, int depth, Symbol const** named) {
    for (; me && depth < ALIAS_CHAIN_MAX; depth++) {
        if (!(my flags & SYMBOL_ALIAS) || !my alias) return nullptr;
        if (!my alias->item) {
            // `export = x` / `export default x`: what the name finds (tsc's getTargetOfExportAssignment marks it)
            if (my alias->class_expr || my alias->entity || !my alias->scope || !span_valid(my alias->name)) return nullptr;
            Symbol const* found = scope_resolve(my alias->scope, span_sv(my alias->name), SYMBOL_ANY_MEANING);
            if (found == me) return nullptr;
            me = found;
            continue;
        }
        symbol_resolve_alias((Symbol*)me); // (what sets source_symbol)
        PortItem const* item = my alias->item;
        if (item->type_only) {
            if (named) *named = item->symbol;
            return item;
        }
        if (my alias->type_only_star) {
            // (What the star brings under the name: tsc's getTypeOnlyAliasDeclaration looks it up in the exports of the
            // module with the `export type *`)
            if (named) *named = item->source_symbol;
            return my alias->type_only_star;
        }
        if (item->tag == PORT_IMPORT_ALIAS) {
            // `import A = a.B`: never type-only itself (tsgo's getTargetOfImportEqualsDeclaration marks nothing), but
            // a pure alias it names passes its mark on (resolveIndirectionAlias); a type-only part further left is
            // the declaration's own error, see port_statement_typecheck
            isize n = vec_symbol_cptr_size(&port_item_alias(item)->path_symbols);
            if (n != vec_span_size(&port_item_alias(item)->path)) return nullptr;
            Symbol const* last = n ? *vec_symbol_cptr_at(&port_item_alias(item)->path_symbols, n - 1) : nullptr;
            if (!last || !(last->flags & SYMBOL_ALIAS) || (last->flags & (SYMBOL_VALUE | SYMBOL_TYPE | SYMBOL_NAMESPACE))) return nullptr;
            me = last;
            continue;
        }
        Symbol const* next = item->source_symbol;
        if (!next && item->module_ref && (item->tag == PORT_IMPORT_GATHER || item->tag == PORT_IMPORT_DEFAULT)) {
            // A namespace, require or synthetic default import of a module with `export =`: what that names (tsc's
            // markSymbolOfAliasDeclarationIfTypeOnly with the export= target)
            Symbol* module = binder_module_of_ref(my binder, item->module_ref);
            next = module ? symbol_get_export(module, c_sv("export=")) : nullptr;
        }
        me = next;
        if (me && me->flags & SYMBOL_VALUE) return nullptr; // Merged with a value along the way
    }
    return nullptr;
}

// Module augmentations (tsc's mergeModuleAugmentation)

void binder_defer_global_augmentation(Binder* me, NamespaceDecl* decl) {
    if (my global_augmentation_count == my global_augmentation_cap) {
        my global_augmentation_cap = my global_augmentation_cap ? my global_augmentation_cap * 2 : 4;
        my global_augmentations = realloc(my global_augmentations, sizeof(NamespaceDecl*) * my global_augmentation_cap);
    }
    my global_augmentations[my global_augmentation_count++] = decl;
}

void binder_declare_global_augmentations(Binder* me) {
    for (u32 i = 0; i < my global_augmentation_count; i++) namespace_decl_add_contents(my global_augmentations[i]);
}

void binder_add_augmentation(Binder* me, NamespaceDecl* decl) {
    if (my augmentation_count == my augmentation_cap) {
        my augmentation_cap = my augmentation_cap ? my augmentation_cap * 2 : 4;
        my augmentations = realloc(my augmentations, sizeof(NamespaceDecl*) * my augmentation_cap);
    }
    my augmentations[my augmentation_count++] = decl;
    // Its name may turn out to be matched by a pattern only: then imports of it get the module this makes, whatever
    // file declares the pattern or the augmentation first (tsc's mergeModuleAugmentation runs before any import)
    csview path = cstr_sv(&decl->module_ref->path);
    u64 hash = symbol_name_hash(path);
    if (decl->module_ref->file || memchr(path.buf, '*', (size_t)path.size) || symbol_table_get_own(&my pattern_augmentations, path, hash)) {
        return;
    }
    Symbol* module = binder_new_symbol(me, span_sv(decl->name), SYMBOL_VALUE_MODULE);
    module->is_module = true;
    module->module = arena_new(binder_arena(me), SymbolModule);
    module->name_span = decl->name;
    symbol_ensure_exports(module);
    symbol_table_insert(&my pattern_augmentations, binder_arena(me), path, hash, module);
}

// tsc's getExcludedSymbolFlags()
static u32 excluded_flags(u32 flags) {
    u32 ret = 0;
    if (flags & SYMBOL_BLOCK_SCOPED_VARIABLE) ret |= SYMBOL_BLOCK_SCOPED_VARIABLE_EXCLUDES;
    if (flags & SYMBOL_FUNCTION_SCOPED_VARIABLE) ret |= SYMBOL_FUNCTION_SCOPED_VARIABLE_EXCLUDES;
    if (flags & SYMBOL_ENUM_MEMBER) ret |= SYMBOL_ENUM_MEMBER_EXCLUDES;
    if (flags & SYMBOL_FUNCTION) ret |= SYMBOL_FUNCTION_EXCLUDES;
    if (flags & SYMBOL_CLASS) ret |= SYMBOL_CLASS_EXCLUDES;
    if (flags & SYMBOL_INTERFACE) ret |= SYMBOL_INTERFACE_EXCLUDES;
    if (flags & SYMBOL_REGULAR_ENUM) ret |= SYMBOL_REGULAR_ENUM_EXCLUDES;
    if (flags & SYMBOL_CONST_ENUM) ret |= SYMBOL_CONST_ENUM_EXCLUDES;
    if (flags & SYMBOL_VALUE_MODULE) ret |= SYMBOL_VALUE_MODULE_EXCLUDES;
    if (flags & SYMBOL_TYPE_PARAMETER) ret |= SYMBOL_TYPE_PARAMETER_EXCLUDES;
    if (flags & SYMBOL_TYPE_ALIAS) ret |= SYMBOL_TYPE_ALIAS_EXCLUDES;
    if (flags & SYMBOL_ALIAS) ret |= SYMBOL_ALIAS_EXCLUDES;
    return ret;
}

// Points the AST node of a declaration at the symbol it now belongs to
static void retarget_decl(SymbolDecl const* decl, Symbol* symbol) {
    switch (decl->kind) {
        case SYMBOL_DECL_VARIABLE: ((Binding*)decl->node)->symbol = symbol; return;
        case SYMBOL_DECL_FUNCTION: ((FunctionImpl*)decl->node)->symbol = symbol; return;
        case SYMBOL_DECL_CLASS: ((ClassDecl*)decl->node)->symbol = symbol; return;
        case SYMBOL_DECL_INTERFACE: ((InterfaceDecl*)decl->node)->symbol = symbol; return;
        case SYMBOL_DECL_ENUM: ((EnumDecl*)decl->node)->symbol = symbol; return;
        case SYMBOL_DECL_ENUM_MEMBER: ((EnumCase*)decl->node)->symbol = symbol; return;
        case SYMBOL_DECL_TYPE_ALIAS: ((TypeDecl*)decl->node)->symbol = symbol; return;
        case SYMBOL_DECL_NAMESPACE: ((NamespaceDecl*)decl->node)->symbol = symbol; return;
        default: impossible;
    }
}

static void merge_into_exports(Binder* binder, Symbol* owner, csview name, Symbol* source, ErrorCtx* errs);

// tsc's mergeSymbol(): source's declarations (and exports) move to target, whose nodes then point at target. Source is
// left behind with target's exports, so that the blocks declared into it see everything
static void merge_symbol(Binder* binder, Symbol* target, Symbol* source, ErrorCtx* errs) {
    if (target == source) return;
    if (target->binder != binder || source->flags & SYMBOL_ALIAS) {
        // TODO: augmenting a symbol of the shared lib program (wants clone_parent_symbol() and a table to put the
        // clone in), and an alias inside an augmentation that meets an existing name
        return;
    }
    if (target->flags & excluded_flags(source->flags)) {
        report_conflicts(errs, target, source->decls[0].name, source->flags, true, (csview){0});
        for (u32 i = 1; i < source->decl_count; i++) report_conflict(errs, source->decls[i].name, 2300);
        return;
    }
    target->decl_flags_before = target->flags;
    target->flags |= source->flags;
    target->is_ambient |= source->is_ambient;
    for (u32 i = 0; i < source->decl_count; i++) {
        symbol_add_decl(target, source->decls[i]);
        retarget_decl(&source->decls[i], target);
    }
    if (source->exports) {
        symbol_ensure_exports(target);
        u32 count;
        SymbolEntry* entries = symbol_table_collect(source->exports, &count);
        for (u32 i = 0; i < count; i++) merge_into_exports(binder, target, symbol_entry_name(&entries[i]), entries[i].symbol, errs);
        free(entries);
        source->exports = target->exports;
    }
}

// Adds source to the exports of owner under name, merging it into what is there already: into the target of an alias,
// into what comes through `export *`
static void merge_into_exports(Binder* binder, Symbol* owner, csview name, Symbol* source, ErrorCtx* errs) {
    u64 hash = symbol_name_hash(name);
    Symbol* existing = symbol_table_get_own(owner->exports, name, hash);
    bool from_star = false;
    if (!existing && owner->is_module && owner->module->export_star_count) {
        existing = (Symbol*)symbol_table_lookup(module_resolved_exports(owner), name, hash);
        from_star = existing != nullptr;
    }
    if (!existing) {
        symbol_table_insert(owner->exports, binder_arena(binder), name, hash, source);
        if (owner->is_module && owner->module->resolved_exports_state == ALIAS_DONE) {
            symbol_table_insert(owner->module->resolved_exports, binder_arena(binder), name, hash, source);
        }
        source->parent = owner;
        return;
    }
    if (symbol_is_pure_alias(existing)) {
        Symbol* alias = existing;
        existing = symbol_resolve_alias(existing);
        if (!existing || existing->is_placeholder) return;
        // (tsc's mergeSymbol resolves the target to compare, and reports a conflict at the alias's declarations)
        if (existing->binder == binder && !(source->flags & SYMBOL_ALIAS) && (existing->flags & excluded_flags(source->flags))) {
            report_conflicts(errs, alias, source->decls[0].name, source->flags, true, (csview){0});
            for (u32 i = 1; i < source->decl_count; i++) report_conflict(errs, source->decls[i].name, 2300);
            // (What mergeSymbol returns then is the source: the name now means the augmentation's declaration)
            i32 own = symbol_table_own_index(owner->exports, name, hash);
            if (own >= 0 && owner->exports->entries[own].symbol == alias) {
                owner->exports->entries[own].symbol = source;
                source->parent = owner;
            }
            return;
        }
    }
    bool conflict = (existing->flags & excluded_flags(source->flags)) != 0;
    bool through_alias = existing != symbol_table_get_own(owner->exports, name, hash) && !from_star;
    merge_symbol(binder, existing, source, errs);
    // (Merged through an alias, the name is then what it resolved to: tsc's mergeSymbol answers the clone of the
    // alias's target, which mergeSymbolTable puts in the table. An augmentation's block looks its own names up there,
    // and skips an export specifier: `export type { A }` augmented with `interface A` must see A)
    if (through_alias && existing->binder == binder && !conflict) {
        i32 own = symbol_table_own_index(owner->exports, name, hash);
        if (own >= 0) owner->exports->entries[own].symbol = existing;
    }
    // (What `export *` brought is then the module's own export too, which the augmentation's blocks see: tsc's
    // mergeModuleAugmentation merges into the resolved export, then the augmentation into the module. After a
    // conflict, the augmentation's own declaration: mergeSymbolTable then adds the source)
    if (from_star && existing->binder == binder && !conflict) {
        symbol_table_insert(owner->exports, binder_arena(binder), name, hash, existing);
    } else if (from_star && conflict) {
        symbol_table_insert(owner->exports, binder_arena(binder), name, hash, source);
        if (owner->module->resolved_exports_state == ALIAS_DONE) {
            i32 at = symbol_table_own_index(owner->module->resolved_exports, name, hash);
            if (at >= 0) owner->module->resolved_exports->entries[at].symbol = source;
            else symbol_table_insert(owner->module->resolved_exports, binder_arena(binder), name, hash, source);
        }
        source->parent = owner;
    }
}

// Whether merging an augmentation's name needs aliases resolved: then it waits until the others are merged, so that
// what resolves through the module sees them
static bool augmentation_entry_needs_resolving(Symbol const* module, csview name) {
    Symbol const* existing = symbol_table_get(module->exports, name);
    if (existing) return (existing->flags & SYMBOL_ALIAS) != 0;
    return module->is_module && module->module->export_star_count;
}

// tsgo's bindCommonJSTypeExports, for every module and ambient module since TypeScript 7.1: the exports that have a
// type or namespace meaning (a CommonJS module's typedefs, a TypeScript module's `export type`/`export namespace` next
// to its `export =`) become exports of the `export=` symbol itself, which is a namespace by them. An aliasing
// `export=` (`module.exports = C`, `export = C`) then has a meaning of its own and isn't a pure alias any more: the
// module's value is still C's (symbol_for_meaning() resolves it), but C isn't what the module is
// (resolveExternalModuleSymbol and the symbol chain's getSymbolIfSameReference stop at the `export=`), nor does C
// get the types (no merge into C: tsc 6's getCommonJsExportEquals is gone)
static void bind_type_exports(Binder* me, Symbol* module) {
    if (!module->is_module || !module->module || module->exports->count < 2 || module->exports->parent) return;
    Symbol* assigned = symbol_table_get_own(module->exports, c_sv("export="), symbol_name_hash(c_sv("export=")));
    if (!assigned || assigned->binder != me) return;
    for (u32 i = 0; i < module->exports->count; i++) {
        SymbolEntry* entry = &module->exports->entries[i];
        if (entry->symbol == assigned || !(entry->symbol->flags & (SYMBOL_TYPE | SYMBOL_NAMESPACE))) continue;
        symbol_ensure_exports(assigned);
        if (!symbol_table_get(assigned->exports, symbol_entry_name(entry))) {
            symbol_table_insert(assigned->exports, binder_arena(me), symbol_entry_name(entry), entry->hash, entry->symbol);
        }
        assigned->flags |= SYMBOL_NAMESPACE_MODULE;
    }
}

// The namespace import a module's `export = ns` names, when tsgo resolves it to a clone (see
// SymbolRare.export_equals_clone), else null
static Symbol* export_equals_clone_import(Symbol* module) {
    Symbol* assigned = symbol_table_get_own(module->exports, c_sv("export="), symbol_name_hash(c_sv("export=")));
    if (!assigned || !(assigned->flags & SYMBOL_ALIAS) || !assigned->alias || assigned->alias->item ||
        assigned->alias->entity || assigned->alias->class_expr || !assigned->alias->scope || !span_valid(assigned->alias->name)
    ) {
        return nullptr;
    }
    // (The scope's own name, not resolved: scope_resolve() sees through aliases)
    Symbol* named = symbol_table_get(scope_locals(assigned->alias->scope), span_sv(assigned->alias->name));
    return named && named != assigned && export_equals_namespace_import_is_clone(named) ? named : nullptr;
}

// The module an augmentation adds to (the module symbol itself, not its `export =`)
static Symbol* augmented_module(Binder* me, NamespaceDecl* decl) {
    ModuleRef* ref = decl->module_ref;
    // (An ambient module of the name first, then the file: tsc's resolveExternalModule)
    Symbol* ret = binder_module_of_ref(me, ref);
    if (ret && ret->is_module && !ret->module->file && memchr(ret->name.buf, '*', (size_t)ret->name.size)) ret = nullptr;
    if (!ret && ref->file) ret = ref->file->scope ? ref->file->scope->container : nullptr;
    // Nobody declares it (TS2664, or silently in an ambient context): tsc's mergeModuleAugmentation merges it into
    // nothing, the augmentation's symbol stays a local of its file. A module of its own, not a global ambient module
    // that other files' imports of the name would find (an augmentation of a file outside the program)
    if (!ret) {
        if (!ref->detached_module) {
            Symbol* module = binder_new_symbol(me, span_sv(decl->name), SYMBOL_VALUE_MODULE);
            module->is_module = true;
            module->module = arena_new(binder_arena(me), SymbolModule);
            module->name_span = decl->name;
            symbol_ensure_exports(module);
            ref->detached_module = module;
        }
        ret = ref->detached_module;
    }
    return ret;
}

// The pattern ambient module a module of pattern_augmentations augments, else null
static Symbol* augmented_pattern(Binder const* me, Symbol const* module) {
    if (!my pattern_augmentations.count || !module->exports || !module->exports->parent) return nullptr;
    for (Binder const* binder = me; binder; binder = binder->parent) {
        for (u32 i = 0; i < binder->pattern_module_count; i++) {
            if (binder->pattern_modules[i]->exports == module->exports->parent) return binder->pattern_modules[i];
        }
    }
    return nullptr;
}

// tsc's unidirectional mergeSymbol of a pattern's export into what an augmentation of a name it matches declares:
// the declarations (and meanings) of both, the pattern's own symbol untouched
static void add_pattern_declarations(Symbol* target, Symbol const* pattern_symbol) {
    if (!pattern_symbol || target->flags & excluded_flags(pattern_symbol->flags)) return;
    target->flags |= pattern_symbol->flags;
    for (u32 i = 0; i < pattern_symbol->decl_count; i++) symbol_add_decl(target, pattern_symbol->decls[i]);
}

static void merge_augmentations(Binder* me) {
    // tsc's resolveExternalModule for an augmentation: a name that leads to JavaScript (no declarations) and no
    // ambient module can't be augmented
    for (u32 i = 0; i < my augmentation_count; i++) {
        NamespaceDecl* decl = my augmentations[i];
        ModuleRef* ref = decl->module_ref;
        // (tsc's resolveExternalModule for the augmentation's name: a CommonJS file can't augment an ES module, TS1479)
        if (ref->file) module_ref_report_require_of_esm(ref, ref->span, decl->inner_scope->binder->errs);
        if (ref->file || binder_module_of_ref(me, ref)) continue;
        // (Augmentations of one name in one file are one symbol, resolved once, at its first declaration: tsc's
        // mergeModuleAugmentation)
        bool repeated = false;
        for (u32 j = 0; j < i && !repeated; j++) {
            NamespaceDecl const* other = my augmentations[j];
            repeated = span_source(other->name) == span_source(decl->name) && cstr_equals_sv(&other->module_ref->path, cstr_sv(&ref->path));
        }
        if (repeated) continue;
        if (ref->failure != MODULE_REF_IS_JS) {
            // (tsc's mergeModuleAugmentation: not for one in an ambient context, a declaration file's)
            Scope const* outer = decl->inner_scope->parent;
            if (outer && !outer->ambient && ref->failure == MODULE_REF_NOT_BUILT) {
                // (tsc's resolveExternalModule says what a referenced project's unbuilt output is first)
                module_ref_report_unresolved(ref, decl->inner_scope->binder->errs);
            } else if (outer && !outer->ambient) {
                error_ctx_error_d(decl->inner_scope->binder->errs, ref->span, 2664, "Invalid module name in augmentation, module '",
                    ref->path, "' cannot be found.");
            }
            continue;
        }
        error_ctx_error_d(decl->inner_scope->binder->errs, ref->span, 2665, "Invalid module name in augmentation. Module '",
            ref->path, "' resolves to an untyped module at '", module_ref_rare(ref)->resolved_path, "', which cannot be augmented.");
    }
    // First what merges without resolving anything, then (in the same order) what needs aliases or `export *`
    // resolved, and augmentations of an `export =` module, which go into what it assigns
    // (Which entries wait is decided before anything merges: a merge can make a name an own export of the module)
    bool** late_entries = calloc(my augmentation_count ? my augmentation_count : 1, sizeof(bool*));
    panic_if(!late_entries, "out of memory");
    // (Those that can't merge, TS2671/TS2649: tsc's mergeModuleAugmentation leaves them symbols of their own)
    bool* unmerged = calloc(my augmentation_count ? my augmentation_count : 1, sizeof(bool));
    panic_if(!unmerged, "out of memory");
    for (int pass = 0; pass < 2; pass++) {
        for (u32 i = 0; i < my augmentation_count; i++) {
            NamespaceDecl* decl = my augmentations[i];
            Symbol* augmentation = decl->symbol;
            Symbol* module = augmented_module(me, decl);
            ErrorCtx* errs = decl->inner_scope->binder->errs;
            bool export_equals = symbol_table_get(module->exports, c_sv("export=")) != nullptr;
            if (export_equals) {
                if (pass == 0) continue;
                Symbol* assigned = module_export_equals(module);
                if (!assigned || (assigned->is_placeholder && !assigned->is_unresolved)) continue;
                            // (tsc's unknownSymbol isn't a namespace either)
                if (!(assigned->flags & SYMBOL_NAMESPACE) || assigned->is_placeholder) {
                    // (tsc's mergeModuleAugmentation, once per augmentation symbol: at its first declaration)
                    if (!augmentation->decl_count || augmentation->decls[0].node == decl) {
                        error_ctx_error_d(errs, decl->module_ref->span, 2671, "Cannot augment module '",
                            decl->module_ref->path, "' because it resolves to a non-module entity.");
                    }
                    unmerged[i] = !assigned->is_placeholder;
                    continue;
                }
                if (assigned->binder != me) continue; // TODO: a namespace of the shared lib program
                // (`export = ns` of an `import * as ns` that tsgo resolves to a clone: the augmentation merges into the
                // clone, made now, SymbolRare.export_equals_clone)
                Symbol* clone_import = export_equals_clone_import(module);
                if (clone_import && !symbol_rare(module)->export_equals_clone) {
                    Symbol* clone = binder_new_symbol(me, assigned->name, assigned->flags);
                    clone->name_span = assigned->name_span;
                    clone->parent = assigned->parent;
                    symbol_ensure_exports(clone);
                    if (assigned->exports) {
                        for (u32 k = 0; k < assigned->exports->count; k++) {
                            SymbolEntry const* entry = &assigned->exports->entries[k];
                            symbol_table_insert(clone->exports, binder_arena(me), symbol_entry_name(entry), entry->hash, entry->symbol);
                        }
                    }
                    symbol_rare_mut(clone)->clone_of_import = clone_import;
                    symbol_rare_mut(clone)->clone_source = assigned;
                    symbol_rare_mut(module)->export_equals_clone = clone;
                    symbol_rare_mut(clone_import)->export_equals_clone = clone;
                    assigned = clone;
                }
                // (tsc's mergeSymbol of the augmentation into it: one with values against a namespace merged with a
                // variable conflicts, reported at the augmentation's name)
                bool instantiated = false;
                for (u32 k = 0; augmentation->exports && k < augmentation->exports->count && !instantiated; k++) {
                    u32 flags = augmentation->exports->entries[k].symbol->flags;
                    instantiated = (flags & SYMBOL_VALUE & ~SYMBOL_VALUE_MODULE) != 0;
                }
                if (instantiated && (assigned->flags & excluded_flags(SYMBOL_VALUE_MODULE))) {
                    if (!augmentation->decl_count || augmentation->decls[0].node == decl) {
                        error_ctx_error_d(errs, decl->module_ref->span, 2649, "Cannot augment module '", assigned->name,
                            "' with value exports because it resolves to a non-module entity.");
                    }
                    unmerged[i] = true;
                    continue;
                }
                module = assigned;
                symbol_ensure_exports(module);
            }
            u32 count;
            SymbolEntry* entries = symbol_table_collect(augmentation->exports, &count);
            if (pass == 0) {
                late_entries[i] = calloc(count ? count : 1, sizeof(bool));
                panic_if(!late_entries[i], "out of memory");
                for (u32 j = 0; j < count; j++) late_entries[i][j] = augmentation_entry_needs_resolving(module, symbol_entry_name(&entries[j]));
            }
            Symbol const* pattern = augmented_pattern(me, module);
            for (u32 j = 0; j < count; j++) {
                bool late = export_equals || !late_entries[i] || late_entries[i][j];
                if (late != (pass == 1)) continue;
                bool shadows = pattern && !symbol_table_get_own(module->exports, symbol_entry_name(&entries[j]), entries[j].hash);
                merge_into_exports(me, module, symbol_entry_name(&entries[j]), entries[j].symbol, errs);
                if (shadows) add_pattern_declarations(entries[j].symbol, symbol_table_get(pattern->exports, symbol_entry_name(&entries[j])));
            }
            free(entries);
        }
    }
    for (u32 i = 0; i < my augmentation_count; i++) free(late_entries[i]);
    free(late_entries);
    // Every block of an augmentation sees the module's exports, and `declare module "m"` is the module
    for (u32 i = 0; i < my augmentation_count; i++) {
        NamespaceDecl* decl = my augmentations[i];
        if (unmerged[i]) {
            // (A value module of its own if instantiated: `typeof import("m")`, tsc's type of the unmerged symbol)
            symbol_add_decl(decl->symbol, (SymbolDecl){
                .kind = SYMBOL_DECL_NAMESPACE, .node = decl, .name = decl->name, .full = decl->full_span,
                .flags = namespace_decl_is_instantiated(decl) ? SYMBOL_VALUE_MODULE : SYMBOL_NAMESPACE_MODULE,
            });
            continue;
        }
        Symbol* module = augmented_module(me, decl);
        Symbol* assigned = module_export_equals(module);
        if (assigned && assigned != module && !assigned->is_placeholder && assigned->flags & SYMBOL_NAMESPACE &&
            assigned->binder == me
        ) {
            module = assigned;
        }
        // (The clone an augmentation of an `export =` namespace import merges into has the declarations its source has
        // when the first one merges: SymbolRare.export_equals_clone)
        Symbol* source = symbol_rare(module)->clone_source;
        if (source && !module->decl_count) {
            for (u32 k = 0; k < source->decl_count; k++) symbol_add_decl(module, source->decls[k]);
        }
        decl->symbol->exports = module->exports;
        decl->symbol = module;
        // (Into what `export =` assigns, an instantiated one makes it a value module: tsc's mergeSymbol adds the
        // augmentation's ValueModule flag to the clone, whose type is then `typeof x`)
        u32 flags = 0;
        if (!module->is_module) {
            flags = namespace_decl_is_instantiated(decl) ? SYMBOL_VALUE_MODULE : SYMBOL_NAMESPACE_MODULE;
            module->flags |= flags;
        }
        symbol_add_decl(module, (SymbolDecl){
            .kind = SYMBOL_DECL_NAMESPACE, .node = decl, .name = decl->name, .full = decl->full_span, .flags = flags,
        });
        // (The name's first augmentation is what tsc clones, the pattern merges into it, then the others)
        if (module->decl_count == 1) add_pattern_declarations(module, augmented_pattern(me, module));
    }
    free(unmerged);
}

bool symbol_is_augmented_export_equals(Symbol const* me) {
    if (my is_module || !(my flags & SYMBOL_NAMESPACE)) return false;
    for (u32 i = 0; i < my decl_count; i++) {
        if (my decls[i].kind == SYMBOL_DECL_NAMESPACE && ((NamespaceDecl const*)my decls[i].node)->is_augmentation) return true;
    }
    return false;
}

void binder_resolve_remaining_aliases(Binder* me) {
    for (u32 i = 0; i < my symbol_count; i++) {
        Symbol* symbol = my symbols[i];
        if (symbol->flags & SYMBOL_ALIAS && symbol->alias->state == ALIAS_UNRESOLVED) symbol_resolve_alias(symbol);
    }
}

bool symbol_const_enum_is_ambient(Symbol const* me) {
    SourceFile const* source = span_source(my name_span);
    if (source && source->reference_preserves_const_enums) return false;
    return my is_ambient || (source && source->reference_const_enums_ambient);
}

// tsc's checkAliasSymbol under verbatimModuleSyntax: an import or export (not type-only, not ambient) of an ambient
// const enum, which only the file that declares it could inline (TS2748; a use of it is then fine, see
// check_const_enum_access())
static void check_alias_ambient_const_enum(Symbol const* alias, Symbol const* target) {
    CompilerOptions const* options = compiler_options();
    if (!options->verbatim_module_syntax || !(target->flags & SYMBOL_CONST_ENUM) || !symbol_const_enum_is_ambient(target)) {
        return;
    }
    PortItem const* item = alias->alias->item;
    Scope const* scope = alias->alias->scope;
    if (item->type_only || item->type_modifier || item->jsdoc || scope->ambient || (scope->file && scope->file->is_declaration)) return;
    Span at = item->decl_name;
    if (item->tag == PORT_IMPORT_ALIAS) {
        at = span_with_semicolon(item->full_span);
    } else if (item->tag == PORT_IMPORT_SPECIFIC || item->tag == PORT_EXPORT_SPECIFIC || item->tag == PORT_REEXPORT_SPECIFIC) {
        if (span_valid(item->source_name)) at = span_between(item->source_name, item->decl_name);
    } else if (item->tag != PORT_IMPORT_DEFAULT) {
        return;
    }
    error_ctx_error_d(binder_errs(scope->binder), at, 2748, "Cannot access ambient const enums when 'verbatimModuleSyntax' is enabled.");
}

// tsc's checkAliasSymbol under isolatedModules/verbatimModuleSyntax: what a transpiler that sees one file at a time
// can't elide (an import or re-export of a type, or of something imported as a type only), and ES syntax in a file
// emitted as CommonJS
static void check_alias_isolated_modules(Symbol const* alias, Symbol const* target) {
    CompilerOptions const* options = compiler_options();
    if (!options->isolated_modules && !options->verbatim_module_syntax) return;
    PortItem const* item = alias->alias->item;
    Scope const* scope = alias->alias->scope;
    SourceFile const* file = scope->file;
    if (item->type_only || item->type_modifier || item->jsdoc || scope->ambient || !file || file->is_declaration) return;
    // (The alias's: a type the namespace merged with the value `export =` names exports, which may have a property of
    // that name too, is a value as well, tsc's combineValueAndTypeSymbols; see SymbolAlias.value_target)
    u32 target_flags = symbol_resolved_flags(target);
    if (alias->alias->value_target) target_flags |= alias->alias->value_target->flags;
    bool is_type = !(target_flags & SYMBOL_VALUE);
    // (a JavaScript file's import of a type is an error of its own)
    if (file->is_js && is_type) return;
    const char* flag = options->verbatim_module_syntax ? "verbatimModuleSyntax" : "isolatedModules";
    ErrorCtx* errs = binder_errs(scope->binder);
    bool import_equals = item->tag == PORT_IMPORT_ALIAS || item->is_require || item->require_binding;
    // tsc's error node: the whole specifier, the whole `import x = ...`, the name of `* as ns`, a default import's
    // whole clause (to its named imports)
    Span at = item->decl_name;
    if (import_equals) {
        at = span_with_semicolon(item->full_span);
    } else if (item->tag == PORT_IMPORT_SPECIFIC || item->tag == PORT_EXPORT_SPECIFIC || item->tag == PORT_REEXPORT_SPECIFIC) {
        if (span_valid(item->source_name)) at = span_between(item->source_name, item->decl_name);
    } else if (item->tag == PORT_REEXPORT_GATHER) {
        at = item->full_span;
    } else if (item->tag == PORT_IMPORT_DEFAULT && span_valid(item->clause)) {
        at = item->clause;
    }
    PortItem const* type_only = symbol_type_only_declaration(alias);
    if (is_type || type_only) {
        // (`{ a as b }` names `a`)
        csview name = port_item_name(span_valid(item->source_name) ? item->source_name : item->decl_name);
        switch (item->tag) {
            case PORT_IMPORT_DEFAULT:
            case PORT_IMPORT_SPECIFIC:
            case PORT_IMPORT_ALIAS:
            case PORT_IMPORT_GATHER:
                if (item->tag == PORT_IMPORT_GATHER && !item->is_require) break; // (not tsc's switch)
                if (options->verbatim_module_syntax) {
                    Error* error = item->tag == PORT_IMPORT_ALIAS ?
                        error_ctx_error_d(errs, at, 1288, "An import alias cannot resolve to a type or type-only "
                            "declaration when 'verbatimModuleSyntax' is enabled.") :
                        is_type ?
                        error_ctx_error_d(errs, at, 1484, "'", name, "' is a type and must be imported using a type-only "
                            "import when 'verbatimModuleSyntax' is enabled.") :
                        error_ctx_error_d(errs, at, 1485, "'", name, "' resolves to a type-only declaration and must be "
                            "imported using a type-only import when 'verbatimModuleSyntax' is enabled.");
                    if (!is_type) port_item_add_type_only_related(error, type_only, name);
                }
                if (is_type && import_equals && item->export_import) {
                    error_ctx_error_d(errs, at, 1269, "Cannot use 'export import' on a type or type-only namespace when '",
                        flag, "' is enabled.");
                }
                break;
            case PORT_EXPORT_SPECIFIC:
            case PORT_REEXPORT_SPECIFIC: {
                // (`import type { A } from "./a"; export { A }` is fine without verbatimModuleSyntax: a transpiler
                // sees it's a type)
                SourceFile const* type_only_file = type_only ? span_source(type_only->decl_name) : nullptr;
                if (!options->verbatim_module_syntax && type_only_file == file) break;
                Error* error = is_type ?
                    error_ctx_error_d(errs, at, 1205, "Re-exporting a type when '", flag,
                        "' is enabled requires using 'export type'.") :
                    error_ctx_error_d(errs, at, 1448, "'", name, "' resolves to a type-only declaration and must be "
                        "re-exported using a type-only re-export when '", flag, "' is enabled.");
                if (!is_type) port_item_add_type_only_related(error, type_only, name);
                break;
            }
            default:
                break;
        }
    }
    if (import_equals) return;
    ModuleKind format = source_file_emit_module_kind(file);
    if (options->verbatim_module_syntax && !file->is_js && format == MODULE_KIND_COMMONJS) {
        source_file_verbatim_cjs_error(file, errs, at, false);
    } else if (options->module == MODULE_KIND_PRESERVE && format == MODULE_KIND_COMMONJS) {
        error_ctx_error_d(errs, at, 1293, "ECMAScript module syntax is not allowed in a CommonJS module when 'module' is set to "
            "'preserve'.");
    }
}

// tsc's initializeTypeChecker merges what later declares a UMD global's name (a script after the module, a
// `declare global` augmentation) with mergeSymbol, which resolves the alias: the declarations go into the module (or
// what its `export =` names), and the global becomes that merged symbol. The binder merged them into the alias:
// move them over, with the module's exports visible inside (a `namespace N` in `declare global` sees N's exports)
static void merge_into_umd_globals(Binder* me) {
    SymbolTable* globals = &my globals;
    for (u32 i = 0; i < globals->count; i++) {
        Symbol* global = globals->entries[i].symbol;
        if (!is_umd_global(global) || !global->alias->scope || global->binder != me) continue;
        bool others = false;
        for (u32 d = 0; d < global->decl_count && !others; d++) others = global->decls[d].kind != SYMBOL_DECL_ALIAS;
        if (!others) continue;
        Symbol* own = global->alias->scope->container;
        if (!own || !own->is_module) continue;
        Symbol* target = module_export_equals(own);
        // (`export = N` naming the global N itself: tsc's mergeSymbol resolves the alias it merges into, which goes
        // through the module's `export =` back to the alias)
        // Only for one that merges after the alias: a `declare global` of the alias's own file (tsc merges the global
        // augmentations last); a script's namespace merged first takes the alias in instead
        bool augmentation_merges = false;
        for (u32 d = 0; d < global->decl_count && target == global; d++) {
            augmentation_merges |= global->decls[d].kind != SYMBOL_DECL_ALIAS && global->alias->item &&
                span_source(global->decls[d].full) == span_source(global->alias->item->full_span);
        }
        // (An `export =` naming an alias that resolves to nothing, `import { Bar } from "./foo"` of a module without
        // Bar: tsc's mergeSymbol resolves the global's alias to the unknown symbol too, the source takes the name)
        Symbol* resolved_target = target && target != global && (target->flags & SYMBOL_ALIAS) ? symbol_resolve_alias(target) : target;
        bool unknown = target && target != global && (target->is_placeholder ||
            ((target->flags & SYMBOL_ALIAS) && (!resolved_target || resolved_target->is_placeholder)));
        bool circular = target == global && global->alias->item && augmentation_merges;
        if (circular && !unknown) {
            error_ctx_error_d(global->alias->scope->binder->errs, span_with_semicolon(global->alias->item->full_span), 2303,
                "Circular definition of import alias '", global->alias->item->decl_name, "'.");
            // (The `export =` resolving to it is on the same cycle: tsgo's resolveAlias fails every alias from the
            // cycle's start, both report)
            Symbol const* export_equals = own->exports ? symbol_table_get(own->exports, c_sv("export=")) : nullptr;
            if (export_equals && (export_equals->flags & SYMBOL_ALIAS) && export_equals->decl_count &&
                span_valid(export_equals->decls[0].full)
            ) {
                error_ctx_error_d(global->alias->scope->binder->errs, span_with_semicolon(export_equals->decls[0].full), 2303,
                    "Circular definition of import alias '", global->alias->item->decl_name, "'.");
            }
        }
        if (circular || unknown) {
            // (mergeSymbol gets the unknown symbol for the alias and returns the source: the global is what merged,
            // the alias is left with its own declaration)
            u32 merged_flags = 0;
            for (u32 d = 0; d < global->decl_count; d++) {
                if (global->decls[d].kind != SYMBOL_DECL_ALIAS) merged_flags |= global->decls[d].flags;
            }
            Symbol* merged = binder_new_symbol(me, global->name, merged_flags);
            merged->is_ambient = global->is_ambient;
            u32 kept = 0;
            for (u32 d = 0; d < global->decl_count; d++) {
                if (global->decls[d].kind == SYMBOL_DECL_ALIAS) {
                    global->decls[kept++] = global->decls[d];
                    continue;
                }
                symbol_add_decl(merged, global->decls[d]);
                retarget_decl(&global->decls[d], merged);
            }
            global->decl_count = kept;
            global->flags = SYMBOL_ALIAS;
            merged->exports = global->exports;
            global->exports = nullptr;
            globals->entries[i].symbol = merged;
            continue;
        }
        if (!target || target->is_placeholder || target->binder != me || (target->flags & SYMBOL_ALIAS)) continue;
        if (!target->is_module && !(target->flags & SYMBOL_NAMESPACE)) continue;
        u32 flags = 0;
        for (u32 d = 0; d < global->decl_count; d++) {
            if (global->decls[d].kind != SYMBOL_DECL_ALIAS) flags |= global->decls[d].flags;
        }
        if (target->flags & excluded_flags(flags)) continue; // TODO: tsc's conflict report against the target
        ErrorCtx* errs = global->alias->scope->binder->errs;
        target->flags |= flags;
        u32 kept = 0;
        for (u32 d = 0; d < global->decl_count; d++) {
            if (global->decls[d].kind == SYMBOL_DECL_ALIAS) {
                global->decls[kept++] = global->decls[d];
                continue;
            }
            symbol_add_decl(target, global->decls[d]);
            retarget_decl(&global->decls[d], target);
        }
        // (The alias is declared by `export as namespace` alone: what merged went into the target, with its meanings)
        global->decl_count = kept;
        global->flags = SYMBOL_ALIAS;
        if (global->exports) {
            symbol_ensure_exports(target);
            u32 count;
            SymbolEntry* entries = symbol_table_collect(global->exports, &count);
            for (u32 j = 0; j < count; j++) merge_into_exports(me, target, symbol_entry_name(&entries[j]), entries[j].symbol, errs);
            free(entries);
        }
        global->exports = target->exports;
        globals->entries[i].symbol = target;
    }
}

// Decides the merges declare_pending_alias_merge() left, in declaration order, like tsc's mergeSymbol: into what the
// name means now (another pending merge may have replaced it), an alias resolved first. An alias that resolves to
// nothing gives way to the declaration; one whose target doesn't exclude it merges it into that target (tsc's clone of
// it, which getMergedSymbol makes what the target is), which the name then means; one whose target excludes it is a
// conflict, and the declaration takes the name
static void binder_link_alias_merges(Binder* me) {
    for (u32 i = 0; i < my pending_alias_merge_count; i++) {
        PendingAliasMerge const* p = &my pending_alias_merges[i];
        Symbol* source = p->source;
        ErrorCtx* errs = binder_errs(me);
        u64 hash = symbol_name_hash(source->name);
        i32 at = symbol_table_own_index(p->table, source->name, hash);
        if (at < 0) {
            symbol_table_insert(p->table, binder_arena(me), source->name, hash, source);
            continue;
        }
        Symbol* target = p->table->entries[at].symbol;
        if (target->flags & excluded_flags(source->flags)) {
            report_conflicts(errs, target, source->decls[0].name, source->flags, false, (csview){0});
            for (u32 d = 1; d < source->decl_count; d++) report_conflict(errs, source->decls[d].name, 2300);
            continue;
        }
        if (target->flags & SYMBOL_ALIAS) {
            Symbol* resolved = symbol_resolve_alias(target);
            if (!resolved || resolved->is_placeholder) {
                p->table->entries[at].symbol = source;
                continue;
            }
            if (resolved->binder != me) {
                // (A symbol of the shared lib program: no clone to merge into, the old way)
                merge_symbol(me, target, source, errs);
                continue;
            }
            if (resolved->flags & excluded_flags(source->flags)) {
                report_conflicts(errs, target, source->decls[0].name, source->flags, true, (csview){0});
                for (u32 d = 1; d < source->decl_count; d++) report_conflict(errs, source->decls[d].name, 2300);
                p->table->entries[at].symbol = source;
                continue;
            }
            target = resolved;
        }
        merge_symbol(me, target, source, errs);
        p->table->entries[at].symbol = target;
    }
    my alias_merges_done = true;
}

void binder_link(Binder* me) {
    binder_link_alias_merges(me);
    // (tsc's initializeTypeChecker merges the globals, a UMD global's `declare global` namespaces with it, before the
    // module augmentations)
    merge_into_umd_globals(me);
    for (u32 i = 0, count = my symbol_count; i < count; i++) {
        if (my symbols[i]->is_module) bind_type_exports(me, my symbols[i]);
    }
    merge_augmentations(me);
    // Resolving makes placeholder symbols: by index
    for (u32 i = 0; i < my symbol_count; i++) {
        Symbol* symbol = my symbols[i];
        if (symbol->is_module) module_resolved_exports(symbol);
    }
    for (u32 i = 0; i < my symbol_count; i++) {
        Symbol* symbol = my symbols[i];
        if (!(symbol->flags & SYMBOL_ALIAS) || !symbol->alias->scope) continue;
        Symbol* target = symbol_resolve_alias(symbol);
        // (An unresolved import merged with an exported value is that value too: tsc's local symbol has ExportValue)
        if (symbol->alias->item && target && target->is_placeholder && symbol->is_local &&
            symbol->alias->scope->container && !port_item_declares_export(symbol->alias->item)
        ) {
            Symbol* exported = symbol_table_get(symbol->alias->scope->container->exports, symbol->name);
            if (exported && exported != symbol && (exported->flags & SYMBOL_VALUE) && !(exported->flags & SYMBOL_ALIAS) &&
                symbol_declared_in_block(exported, symbol->alias->scope)
            ) {
                symbol->alias->export_value = exported;
            }
        }
        if (!symbol->alias->item || !target || target->is_placeholder) continue;
        // (A UMD global, `export as namespace foo`, isn't one tsc's checkAliasSymbol checks)
        if (symbol->alias->item->tag == PORT_EXPORT_AS_NAMESPACE) continue;
        check_alias_ambient_const_enum(symbol, target);
        check_alias_isolated_modules(symbol, target);
        // In tsc an exported declaration also has a local symbol, which an import of the same name conflicts with
        u32 local_flags = symbol->flags;
        Symbol const* container = symbol->alias->scope->container;
        if (container && !port_item_declares_export(symbol->alias->item)) {
            Symbol* exported = symbol_table_get(container->exports, symbol->name);
            // (Not what the import resolves to: an augmentation merged through `export { A }` of an imported A makes
            // the export its target, see merge_into_exports())
            if (exported == target) exported = nullptr;
            // (The local symbol is the declaring block's: another `declare module "m" {}` block's export isn't)
            if (exported && container->is_module && !container->module->file && !symbol_declared_in_block(exported, symbol->alias->scope)) {
                exported = nullptr;
            }
            if (exported && exported != symbol) local_flags |= exported->flags & ~(u32)SYMBOL_ALIAS;
            if (exported && exported != symbol && (exported->flags & SYMBOL_VALUE) && !(exported->flags & SYMBOL_ALIAS)) {
                symbol->alias->export_value = exported;
            }
        }
        if (!(local_flags & SYMBOL_ANY_MEANING)) continue;
        // An alias merged with local declarations: fine as long as they mean different things (tsc: checkAliasSymbol)
        u32 target_flags = symbol_resolved_flags(target);
        u32 excluded = (target_flags & SYMBOL_VALUE ? SYMBOL_VALUE : 0) |
            (target_flags & SYMBOL_TYPE ? SYMBOL_TYPE : 0) |
            (target_flags & SYMBOL_NAMESPACE ? SYMBOL_NAMESPACE : 0);
        PortItem const* item = symbol->alias->item;
        ErrorCtx* errs = symbol->alias->scope->binder->errs;
        // tsc reports on the import specifier (`a as b`), or the whole `import a = b.c`
        Span at = item->decl_name;
        if (item->tag == PORT_IMPORT_ALIAS) at = span_with_semicolon(item->full_span);
        if (item->tag == PORT_IMPORT_SPECIFIC && span_valid(item->source_name)) {
            at = span_between(item->source_name, item->decl_name);
        }
        if (!(local_flags & excluded)) {
            // An import merged with a local value looks like a value to a transpiler (isolatedModules itself, not
            // verbatimModuleSyntax, which says the import needs `type` anyway: the option as written, which
            // verbatimModuleSyntax's default doesn't set, tsc's compilerOptions.IsolatedModules.IsTrue())
            CompilerOptions const* written = compiler_options();
            if (written->is_set[COMPILER_OPTION_isolated_modules] && written->isolated_modules && !port_item_is_export(item) && !item->type_only && !item->jsdoc &&
                !item->type_modifier && local_flags & SYMBOL_VALUE
            ) {
                error_ctx_error_d(errs, at, 2865, "Import '", item->decl_name, "' conflicts with local value, so must "
                    "be declared with a type-only import when 'isolatedModules' is enabled.");
            }
            continue;
        }
        // A later file's declaration merged into it (a global namespace's members): tsc's mergeSymbol resolves the
        // alias it merges into and reports the conflict as a duplicate on both sides (reportMergeSymbolError)
        bool cross_file = false;
        for (u32 d = 1; d < symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_ALIAS; d++) {
            SymbolDecl const* other = &symbol->decls[d];
            if (other->kind == SYMBOL_DECL_ALIAS || !(other->flags & excluded) || span_source(other->full) == span_source(symbol->decls[0].full)) {
                continue;
            }
            // (reportMergeSymbolError's message: an enum's TS2567, a block-scoped variable's TS2451)
            unsigned code = other->flags & SYMBOL_ENUM ? 2567 : other->flags & SYMBOL_BLOCK_SCOPED_VARIABLE ? 2451 : 2300;
            Error* first = report_conflict_named(errs, symbol->decls[0].name, code, (csview){0}, false, true);
            add_duplicate_related(first, symbol->decls[0].name, other->name);
            Error* second = report_conflict_named(errs, other->name, code, (csview){0}, false, true);
            add_duplicate_related(second, other->name, symbol->decls[0].name);
            cross_file = true;
        }
        if (cross_file) continue;
        // (An `@import` too: tsgo reparses it into an import declaration, checkAliasSymbol, a `@typedef` conflicts)
        if (port_item_is_export(item)) {
            error_ctx_error_d(errs, item->full_span, 2484,
                "Export declaration conflicts with exported declaration of '", item->decl_name, "'.");
        } else {
            error_ctx_error_d(errs, at, 2440,
                "Import declaration conflicts with local declaration of '", item->decl_name, "'.");
        }
    }
    for (u32 i = 0; i < my pending_export_merge_count; i++) {
        PendingExportMerge const* pending = &my pending_export_merges[i];
        check_export_merge(pending->scope, pending->block, pending->local, true);
    }
    free(my pending_export_merges);
    my pending_export_merges = nullptr;
    my pending_export_merge_count = my pending_export_merge_cap = 0;
}

// Lookup

static bool is_export_specifier_alias(Symbol const* me) {
    return my flags == SYMBOL_ALIAS && my alias->item && port_item_is_export(my alias->item);
}

static bool symbol_has_meaning(Symbol const* me, u32 meaning) {
    if (my flags & meaning || my is_placeholder) return true;
    if (!(my flags & SYMBOL_ALIAS)) return false;
    if (symbol_resolved_flags(me) & meaning) return true;
    // An import of a property of what `export =` names is looked up as a type too: whether it is one is the checker's
    // (lower.c export_equals_property_type()), and a missing one is tsc's unknownSymbol, which has every meaning. So
    // vite's CJS `export = module` (any) makes `import type { Plugin } from "vite"` the error type, not lib.dom's
    // global `Plugin` (feathers/docs)
    // (Only an `import { X }`: JavaScript's `const { X } = require()` stays a value, a `@typedef X` of the file
    // names the type, webpack's syntax-printer.js)
    PortItem const* item = my alias->item;
    if (!(meaning & SYMBOL_TYPE) || !item || item->tag != PORT_IMPORT_SPECIFIC || item->require_binding) return false;
    Symbol const* target = symbol_resolve_alias((Symbol*)me);
    return target && target != me && target->decl_count && target->decls[0].kind == SYMBOL_DECL_EXPORT_EQUALS_PROPERTY;
}

bool scope_hides_base_expression_type_parameter(Scope const* me, Symbol const* symbol) {
    if (!symbol || !(symbol->flags & SYMBOL_TYPE_PARAMETER)) return false;
    for (Scope const* scope = me; scope; scope = scope->parent) {
        if (scope_declares_symbol(scope, symbol)) return false;
        // (And a class's computed member name doesn't see the class's type parameters as types either: tsc's
        // resolveNameHelper at a ComputedPropertyName returns nothing for them, so `[O]` is TS2304, not TS2693)
        Generic const* generics[2] = { scope_rare(scope)->base_expression_of, scope_rare(scope)->computed_name_of };
        for (u32 g = 0; g < 2; g++) {
            Generic const* generic = generics[g];
            for (u32 i = 0; generic && i < generic->items.size; i++) {
                if (generic->items.data[i].owned_symbol == symbol || generic->items.data[i].symbol_ref == symbol) return true;
            }
        }
    }
    return false;
}

bool scope_declares_symbol(Scope const* me, Symbol const* symbol) {
    u64 hash = symbol_name_hash(symbol->name);
    SymbolTable const* table = my kind == SCOPE_GLOBAL ? &my binder->globals : scope_locals(me);
    if ((table->count || table->parent) && symbol_table_lookup(table, symbol->name, hash) == symbol) return true;
    return my container && my container->exports && symbol_table_lookup(my container->exports, symbol->name, hash) == symbol;
}

// A string's code points for the spelling suggestions (tsgo's []rune(name)); `stack` if they fit
static u32 const* spelling_runes(csview s, u32* stack, isize stack_len, isize* len) {
    isize n = 0;
    for (isize i = 0; i < s.size; i++) n += ((unsigned char)s.buf[i] & 0xC0) != 0x80;
    u32* out = n <= stack_len ? stack : malloc(sizeof(u32) * (size_t)(n ? n : 1));
    panic_if(!out, "out of memory");
    isize k = 0;
    for (isize i = 0; i < s.size && k < n;) {
        unsigned char b = (unsigned char)s.buf[i];
        isize size = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        if (i + size > s.size) size = 1;
        u32 c = size == 1 ? b : b & (0xFF >> (size + 1));
        for (isize j = 1; j < size; j++) c = (c << 6) | ((unsigned char)s.buf[i + j] & 0x3F);
        out[k++] = c;
        i += size;
    }
    *len = k;
    return out;
}

static u32 rune_lower(u32 c) {
    if (c < 0x80) return c >= 'A' && c <= 'Z' ? c + 32 : c;
    return unicode_simple_lower(c);
}

// tsc's levenshteinWithMax over code points (tsgo's runes): case differences cost 0.1. A negative result: more than max
static double levenshtein_with_max(u32 const* s1, isize s1_len, u32 const* s2, isize n, double max) {
    double stack[2][64] = {0};
    double* previous = n + 1 <= 64 ? stack[0] : malloc(sizeof(double) * (size_t)(n + 1));
    double* current = n + 1 <= 64 ? stack[1] : malloc(sizeof(double) * (size_t)(n + 1));
    double big = max + 0.01;
    for (isize i = 0; i <= n; i++) previous[i] = (double)i;
    double result = -1;
    bool gave_up = false;
    for (isize i = 1; i <= s1_len && !gave_up; i++) {
        u32 c1 = s1[i - 1];
        isize min_j = (isize)ceil((double)i > max ? (double)i - max : 1);
        isize max_j = (isize)floor((double)n > max + (double)i ? max + (double)i : (double)n);
        current[0] = (double)i;
        double col_min = (double)i;
        for (isize j = 1; j < min_j; j++) current[j] = big;
        for (isize j = min_j; j <= max_j; j++) {
            u32 c2 = s2[j - 1];
            double substitution = rune_lower(c1) == rune_lower(c2) ? previous[j - 1] + 0.1 : previous[j - 1] + 2;
            double dist = c1 == c2 ? previous[j - 1] : fmin(fmin(previous[j] + 1, current[j - 1] + 1), substitution);
            current[j] = dist;
            if (dist < col_min) col_min = dist;
        }
        for (isize j = max_j + 1; j <= n; j++) current[j] = big;
        if (col_min > max) gave_up = true;
        double* temp = previous;
        previous = current;
        current = temp;
    }
    if (!gave_up) result = previous[n] > max ? -1 : previous[n];
    if (n + 1 > 64) {
        free(previous);
        free(current);
    }
    return result;
}

static bool equals_ignoring_case(csview a, csview b) {
    if (a.size != b.size) return false;
    for (isize i = 0; i < a.size; i++) {
        if (tolower((unsigned char)a.buf[i]) != tolower((unsigned char)b.buf[i])) return false;
    }
    return true;
}

isize spelling_suggestion(csview name, csview const* names, isize count) {
    return spelling_suggestion_ex(name, names, count, nullptr, nullptr);
}

int spelling_compare_strings(void* ctx, isize a, isize b) {
    csview const* names = ctx;
    isize n = names[a].size < names[b].size ? names[a].size : names[b].size;
    int c = memcmp(names[a].buf, names[b].buf, (size_t)n);
    if (c) return c;
    return names[a].size < names[b].size ? -1 : names[a].size > names[b].size;
}

// tsgo's getSpellingSuggestion: of the candidates at the best distance, the least by `compare` (null: the first one,
// tsc 6's rule, for callers whose tsgo order isn't ported)
isize spelling_suggestion_ex(csview name, csview const* names, isize count, int (*compare)(void* ctx, isize a, isize b), void* ctx) {
    // (tsgo counts the name in runes, but compares that with the candidate's length in bytes)
    u32 name_stack[64], candidate_stack[64];
    isize name_len;
    u32 const* name_runes = spelling_runes(name, name_stack, 64, &name_len);
    isize maximum_length_difference = name_len * 34 / 100 > 2 ? name_len * 34 / 100 : 2;
    double best_distance = (double)(name_len * 4 / 10) + 1;
    isize best = -1;
    for (isize i = 0; i < count; i++) {
        csview candidate = names[i];
        if (!candidate.buf) continue;
        isize difference = candidate.size > name_len ? candidate.size - name_len : name_len - candidate.size;
        if (difference > maximum_length_difference || csview_equals_sv(candidate, name)) continue;
        // Short names only when they differ by case
        if (candidate.size < 3 && !equals_ignoring_case(candidate, name)) continue;
        isize candidate_len;
        u32 const* candidate_runes = spelling_runes(candidate, candidate_stack, 64, &candidate_len);
        // (With a comparison, a tie is a candidate too: levenshteinWithMax at the best distance itself)
        double distance = levenshtein_with_max(name_runes, name_len, candidate_runes, candidate_len,
            compare && best >= 0 ? best_distance : best_distance - 0.1);
        if (candidate_runes != candidate_stack) free((void*)candidate_runes);
        if (distance < 0) continue;
        if (compare && best >= 0 && distance >= best_distance - 0.05) {
            if (compare(ctx, i, best) < 0) best = i;
            continue;
        }
        best_distance = distance;
        best = i;
    }
    if (name_runes != name_stack) free((void*)name_runes);
    return best;
}


// tsgo's compareSymbols for the spelling suggestion's tie-break: by the first declaration (the file's place in the
// program, then the position), declared ones first, then by name
typedef struct SuggestCompare {
    csview const* names;
    SymbolEntry const* entries;
    u32 extra; // The leading candidates without a symbol (the primitives)
} SuggestCompare;

static int suggest_compare_symbols(void* ctx, isize a, isize b) {
    SuggestCompare const* me = ctx;
    Symbol const* sa = a >= my extra ? my entries[a - my extra].symbol : nullptr;
    Symbol const* sb = b >= my extra ? my entries[b - my extra].symbol : nullptr;
    Span da = sa && sa->decl_count ? sa->decls[0].name : span_null;
    Span db = sb && sb->decl_count ? sb->decls[0].name : span_null;
    bool ha = span_valid(da) && span_source(da), hb = span_valid(db) && span_source(db);
    if (ha && hb) {
        if (span_source(da) != span_source(db)) return span_source(da)->program_order < span_source(db)->program_order ? -1 : 1;
        if (da.buf != db.buf) return da.buf < db.buf ? -1 : 1;
    } else if (ha != hb) {
        return ha ? -1 : 1;
    }
    return spelling_compare_strings((void*)my names, a, b);
}

// tsc's getSuggestionForSymbolNameLookup on one table
// `exports`: a module's or namespace's exports, where `export default <expr>` is tsc's SymbolFlags.Property (no module
// member)
static bool suggest_in_table(SymbolTable const* table, bool globals, bool exports, csview name, u32 meaning, Suggestion* out) {
    u32 count = 0;
    SymbolEntry* entries = symbol_table_collect(table, &count);
    // (tsc's globals start with its undefinedSymbol, a value)
    static char const* const primitives[] = { "undefined", "string", "number", "boolean", "object", "bigint", "symbol" };
    u32 extra = globals ? c_arraylen(primitives) : 0;
    csview* names = malloc(sizeof(csview) * (count + extra + 1));
    panic_if(!names, "out of memory");
    for (u32 i = 0; i < extra; i++) {
        names[i] = (csview){0};
        if (i == 0) {
            if (meaning & SYMBOL_PROPERTY) names[i] = csview_from(primitives[i]); // (Its flags)
            continue;
        }
        if (!(meaning & SYMBOL_TYPE_ALIAS)) continue;
        char capitalized[16];
        size_t len = strlen(primitives[i]);
        memcpy(capitalized, primitives[i], len);
        capitalized[0] = (char)toupper((unsigned char)capitalized[0]);
        if (symbol_table_get(table, csview_from_n(capitalized, (isize)len))) names[i] = csview_from(primitives[i]);
    }
    for (u32 i = 0; i < count; i++) {
        Symbol const* symbol = entries[i].symbol;
        csview candidate = symbol_entry_name(&entries[i]);
        // (Keyword types like `unknown` are no symbols in tsc)
        bool builtin = symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_BUILTIN;
        bool fits = !symbol->is_placeholder && !builtin && candidate.size && candidate.buf[0] != '"' && candidate.buf[0] != '\'' &&
            ((symbol->flags & meaning) || ((symbol->flags & SYMBOL_ALIAS) &&
                // (Not an alias in flight itself: tsc's tryResolveAlias)
                symbol->alias->state != ALIAS_IN_FLIGHT && symbol->alias->state != ALIAS_CIRCULAR &&
                (symbol_resolved_flags(symbol) & meaning)));
        // (An alias of nothing is tsc's unknownSymbol, a Property: no module member)
        // (Elsewhere too, unknownSymbol only has a value meaning: an import from a module that isn't found is no
        // namespace, `React.X` isn't "Did you mean 'act'?")
        if (fits && (symbol->flags & SYMBOL_ALIAS) && !(symbol->flags & meaning)) {
            Symbol const* target = symbol_resolve_alias((Symbol*)symbol);
            if (!target || (target->is_placeholder && target->is_unresolved)) fits = !exports && (meaning & SYMBOL_PROPERTY);
        }
        if (fits && exports && symbol->decl_count) {
            bool expression = true;
            for (u32 d = 0; d < symbol->decl_count && expression; d++) expression = symbol->decls[d].kind == SYMBOL_DECL_EXPR;
            fits = !expression;
        }
        names[extra + i] = fits ? candidate : (csview){0};
    }
    SuggestCompare cmp = { .names = names, .entries = entries, .extra = extra };
    isize best = spelling_suggestion_ex(name, names, count + extra, suggest_compare_symbols, &cmp);
    if (best >= 0) *out = (Suggestion){ .name = names[best], .symbol = best >= extra ? entries[best - extra].symbol : nullptr };
    free(names);
    free(entries);
    return best >= 0;
}

// tsc's resolveNameForSymbolSuggestion: the innermost table with a close enough name decides
static bool scope_suggest(Scope const* me, csview name, u32 meaning, Suggestion* out) {
    for (Scope const* scope = me; scope; scope = scope->parent) {
        bool globals = scope->kind == SCOPE_GLOBAL;
        SymbolTable const* table = globals ? &scope->binder->globals : scope_locals(scope);
        if ((table->count || table->parent || globals) && suggest_in_table(table, globals, false, name, meaning, out)) return true;
        // (A module's or namespace's exports by tsc's `meaning & SymbolFlags.ModuleMember`: not `export default <expr>`,
        // a property; an enum's by `meaning & SymbolFlags.EnumMember`)
        u32 const module_member = SYMBOL_VARIABLE | SYMBOL_FUNCTION | SYMBOL_CLASS | SYMBOL_INTERFACE | SYMBOL_ENUM |
            SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE | SYMBOL_TYPE_ALIAS | SYMBOL_ALIAS;
        u32 exports_meaning = scope->container && (scope->container->flags & SYMBOL_ENUM) &&
            !(scope->container->flags & (SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE)) ? meaning & SYMBOL_ENUM_MEMBER : meaning & module_member;
        if (scope->container && scope->container->exports && scope->kind != SCOPE_GLOBAL && exports_meaning &&
            suggest_in_table(scope->container->exports, false, true, name, exports_meaning, out)
        ) {
            // (A value meaning: the type meaning has no variables. Only the declaration block's own locals have the
            // local: another block of a merged namespace finds the export itself)
            bool here = false;
            for (u32 d = 0; out->symbol && d < out->symbol->decl_count && !here; d++) here = out->symbol->decls[d].block == scope;
            // (An enum's members have no locals: the member itself, with its declaration)
            bool enum_member = out->symbol && (out->symbol->flags & SYMBOL_ENUM_MEMBER);
            out->export_local = (meaning & SYMBOL_VARIABLE) != 0 && (here || !out->symbol) && !enum_member;
            return true;
        }
    }
    return false;
}

static bool name_is_one_of(csview name, char const* const* names, u32 count) {
    for (u32 i = 0; i < count; i++) {
        if (csview_equals(name, names[i])) return true;
    }
    return false;
}

// tsc's getCannotFindNameDiagnosticForName (but `await` as a callee and shorthand properties)
static void error_cannot_find_name(ErrorCtx* errs, Span name, char const* lib, bool shorthand) {
    static char const* const dom[] = { "document", "console" };
    static char const* const test_runner[] = { "beforeEach", "describe", "suite", "it", "test" };
    static char const* const node[] = { "process", "require", "Buffer", "module", "NodeJS" };
    static char const* const later_lib[] = {
        "Map", "Set", "Promise", "Symbol", "WeakMap", "WeakSet", "Iterator", "AsyncIterator", "SharedArrayBuffer",
        "Atomics", "AsyncIterable", "AsyncIterableIterator", "AsyncGenerator", "AsyncGeneratorFunction", "BigInt",
        "Reflect", "BigInt64Array", "BigUint64Array",
    };
    // (tsc 6.0's usesWildcardTypes: the long message unless `types` has "*")
    bool types = true;
    for (isize i = 0; i < compiler_options()->types.count; i++) {
        if (cstr_equals(&compiler_options()->types.items[i], "*")) types = false;
    }
    csview n = span_sv(name);
    if (name_is_one_of(n, dom, 2)) {
        error_ctx_error_d(errs, name, 2584, "Cannot find name '", name, "'. Do you need to change your target library? "
            "Try changing the 'lib' compiler option to include 'dom'.");
    } else if (csview_equals(n, "$")) {
        error_ctx_error_d(errs, name, types ? 2592 : 2581, "Cannot find name '", name, "'. Do you need to install type "
            "definitions for jQuery? Try `npm i --save-dev @types/jquery`",
            types ? " and then add 'jquery' to the types field in your tsconfig." : ".");
    } else if (name_is_one_of(n, test_runner, 5)) {
        error_ctx_error_d(errs, name, types ? 2593 : 2582, "Cannot find name '", name, "'. Do you need to install type "
            "definitions for a test runner? Try `npm i --save-dev @types/jest` or `npm i --save-dev @types/mocha`",
            types ? " and then add 'jest' or 'mocha' to the types field in your tsconfig." : ".");
    } else if (name_is_one_of(n, node, 5)) {
        error_ctx_error_d(errs, name, types ? 2591 : 2580, "Cannot find name '", name, "'. Do you need to install type "
            "definitions for node? Try `npm i --save-dev @types/node`",
            types ? " and then add 'node' to the types field in your tsconfig." : ".");
    } else if (csview_equals(n, "Bun")) {
        error_ctx_error_d(errs, name, types ? 2868 : 2867, "Cannot find name '", name, "'. Do you need to install type "
            "definitions for Bun? Try `npm i --save-dev @types/bun`",
            types ? " and then add 'bun' to the types field in your tsconfig." : ".");
    } else if (lib && name_is_one_of(n, later_lib, sizeof(later_lib) / sizeof(later_lib[0]))) {
        error_ctx_error_d(errs, name, 2583, "Cannot find name '", name, "'. Do you need to change your target library? "
            "Try changing the 'lib' compiler option to '", lib, "' or later.");
    } else if (shorthand) {
        error_ctx_error_d(errs, name, 18004, "No value exists in scope for the shorthand property '", name,
            "'. Either declare one or provide an initializer.");
    } else {
        error_ctx_error_d(errs, name, 2304, "Cannot find name '", name, "'.");
    }
}

static bool report_misspelled(Scope const* me, Span at, csview name, u32 meaning, bool namespace, ErrorCtx* errs);

static void report_not_found(Scope const* me, Span name, u32 meaning, bool shorthand, ErrorCtx* errs) {
    // A global of a later lib is said to be so rather than misspelled
    char const* lib = lib_feature_of_global(span_sv(name));
    if (lib) {
        error_cannot_find_name(errs, name, lib, shorthand);
        return;
    }
    if (report_misspelled(me, name, span_sv(name), meaning, false, errs)) return;
    error_cannot_find_name(errs, name, nullptr, shorthand);
}

// What a resolve shard leaves to resolve_shard_merge(): a name not found (its suggestion looks at the globals of every
// file), or an expando whose host other files share
typedef enum DeferredKind {
    DEFERRED_NOT_FOUND, // scope_report_not_found()
    DEFERRED_MISSPELLED, // report_misspelled_or(), `fallback` if there is no suggestion
    DEFERRED_EXPANDO, // add_expando_member()
    // A JavaScript file's assignment declarations, all of them: the file's walk then writes no symbol other files see
    DEFERRED_JS_EXPANDO, // add_expando()
    DEFERRED_JS_DEFINE_PROPERTY, // add_define_property()
    DEFERRED_JS_THIS_PROPERTY, // add_this_property()
    DEFERRED_FUNCTION_EXPORT, // add_function_export_member(): `right` the expression, `namespace` whether `export =`
} DeferredKind;

typedef struct DeferredEvent {
    DeferredKind kind;
    Scope* scope;
    ErrorCtx* errs;
    Span at;
    csview name;
    u32 meaning;
    bool shorthand;
    bool namespace;
    MisspelledFallback fallback;
    // DEFERRED_EXPANDO
    Expr* object;
    Span member;
    Expr* left;
    Expr* right;
    TypeNode const* jsdoc_type;
    Expr const* late_bound_key;
    AssignmentDeclKind assignment;
    // The walk hadn't reached `left` yet (scope_add_expandos() at a block's start): it would have linked the
    // declaration then, expr_link_assignment_declaration(), which the replay does instead
    bool link;
    // DEFERRED_JS_*
    BinaryOpExpr* op;
    uint8_t jsdoc_modifiers;
} DeferredEvent;

// Where a shard's report that outlives the walk goes: the walk's contexts other than the file's are scratch ones it
// drops (a `return` outside a function, `super<T>`)
static ErrorCtx* shard_lasting_errs(ResolveShard* shard, ErrorCtx* errs) {
    return !errs || errs == &shard->errs ? errs : &shard->discarded;
}

static DeferredEvent* defer_event(ResolveShard* shard, DeferredKind kind, Scope const* scope, ErrorCtx* errs) {
    DeferredEvent* ret = list_push((void**)&shard->deferred, &shard->deferred_count, &shard->deferred_cap, sizeof(DeferredEvent));
    *ret = (DeferredEvent){ .kind = kind, .scope = (Scope*)scope, .errs = shard_lasting_errs(shard, errs) };
    return ret;
}

// The shard of a JavaScript file's walk (plan 45 stage 2), whose assignment declarations wait for the replay: their
// hosts may be other files' (globals, what aliases of other files resolve to), and most of them read what earlier ones
// declared, so they all go in walk order
static ResolveShard* js_shard(Scope const* me) {
    ResolveShard* shard = binder_shard(my binder);
    return shard && my file && my file->is_js ? shard : nullptr;
}

// Whether the walk has yet to reach the assignment target `left` (see DeferredEvent.link)
static bool walk_unvisited(Expr const* left) {
    return left->tag == EXPR_PROPERTY && !left->property->context;
}

void scope_report_not_found(Scope const* me, Span name, u32 meaning, bool shorthand, ErrorCtx* errs) {
    ResolveShard* shard = binder_shard(my binder);
    if (shard) {
        DeferredEvent* event = defer_event(shard, DEFERRED_NOT_FOUND, me, errs);
        event->at = name;
        event->meaning = meaning;
        event->shorthand = shorthand;
        return;
    }
    report_not_found(me, name, meaning, shorthand, errs);
}

static void report_misspelled_fallback(Span at, csview name, MisspelledFallback fallback, ErrorCtx* errs) {
    if (fallback == FALLBACK_NAMESPACE) {
        error_ctx_error_d(errs, at, 2503, "Cannot find namespace '", at, "'.");
    } else {
        error_ctx_error_d(errs, at, 2874, "This JSX tag requires '", name, "' to be in scope, but it could not be found.");
    }
}

// A misspelling of `name` (which `at` needs) if there is one, else `fallback`
static void report_misspelled_or(
    Scope const* me, Span at, csview name, u32 meaning, bool namespace, MisspelledFallback fallback, ErrorCtx* errs
) {
    ResolveShard* shard = binder_shard(my binder);
    if (shard) {
        DeferredEvent* event = defer_event(shard, DEFERRED_MISSPELLED, me, errs);
        event->at = at;
        event->name = name;
        event->meaning = meaning;
        event->namespace = namespace;
        event->fallback = fallback;
        return;
    }
    if (!report_misspelled(me, at, name, meaning, namespace, errs)) {
        report_misspelled_fallback(at, name, fallback, errs);
    }
}

// (A suggestion's diagnostic sorts by its own code: tsgo's CompareDiagnostics has no canonicalHead)
static bool report_misspelled(Scope const* me, Span at, csview name, u32 meaning, bool namespace, ErrorCtx* errs) {
    // (Not where nothing is reported, a skipLibCheck declaration file's: a suggestion looks at every name in scope)
    if (!errs || (span_source(at) && !source_file_is_checked(span_source(at)))) return false;
    Suggestion suggestion = {0};
    if (!scope_suggest(me, name, meaning, &suggestion)) return false;
    Error* error = namespace ?
        error_ctx_error_d(errs, at, 2833, "Cannot find namespace '", name, "'. Did you mean '", suggestion.name, "'?") :
        error_ctx_error_d(errs, at, 2552, "Cannot find name '", name, "'. Did you mean '", suggestion.name, "'?");
    Symbol const* symbol = suggestion.export_local ? nullptr : suggestion.symbol;
    for (u32 i = 0; symbol && i < symbol->decl_count; i++) {
        if (!(symbol->decls[i].flags & SYMBOL_VALUE)) continue;
        // (tsc's error span of the declaration: its name, but all of a parameter, `...` and modifiers included)
        SymbolDecl const* decl = &symbol->decls[i];
        bool parameter = decl->kind == SYMBOL_DECL_VARIABLE && ((Binding const*)decl->node)->is_parameter &&
            !((Binding const*)decl->node)->parent && span_valid(decl->full);
        error_add_related_d(error, parameter ? decl->full : decl->name, 2728, "'", suggestion.name, "' is declared here.");
        break;
    }
    return true;
}

void scope_report_jsx_factory_not_found(Scope const* me, Span at, csview name, u32 meaning, ErrorCtx* errs) {
    report_misspelled_or(me, at, name, meaning, false, FALLBACK_JSX_FACTORY, errs);
}

void scope_report_namespace_not_found(Scope const* me, Span at, ErrorCtx* errs) {
    report_misspelled_or(me, at, span_sv(at), SYMBOL_NAMESPACE, true, FALLBACK_NAMESPACE, errs);
}

typedef struct PendingNotFound {
    Scope const* scope;
    Span name;
    bool shorthand;
    ErrorCtx* errs;
} PendingNotFound;

void scope_report_value_not_found(Scope const* me, Span name, bool shorthand, ErrorCtx* errs) {
    bool in_class = false;
    for (Scope const* scope = me; scope && !in_class; scope = scope->parent) in_class = scope_rare(scope)->this_ref.class_decl;
    if (!in_class || (my flow && my flow->in_type_query)) {
        ClassItem const* field = in_class ? scope_invalid_initializer_field(me, span_sv(name), nullptr) : nullptr;
        if (field) {
            scope_report_invalid_initializer_reference(field, name, errs);
            return;
        }
        scope_report_not_found(me, name, SYMBOL_VALUE, shorthand, errs);
        return;
    }
    // (A member of a class around it needs types to tell: reported once they are, scope_report_missing_prefixes())
    Binder* binder = my binder;
    ResolveShard* shard = binder_shard(binder);
    if (shard) {
        *(PendingNotFound*)list_push((void**)&shard->pending_not_found, &shard->pending_not_found_count,
            &shard->pending_not_found_cap, sizeof(PendingNotFound)) = (PendingNotFound){ me, name, shorthand, shard_lasting_errs(shard, errs) };
        return;
    }
    *(PendingNotFound*)list_push((void**)&binder->pending_not_found, &binder->pending_not_found_count,
        &binder->pending_not_found_cap, sizeof(PendingNotFound)) = (PendingNotFound){ me, name, shorthand, errs };
}

// tsc's checkAndReportErrorForMissingPrefix: from the `this` container (a class member, or a function in one) out, a
// static member of a class around it, or an instance member of the class it is directly a member of
static bool report_missing_prefix(Scope const* me, Span name, ErrorCtx* errs) {
    Checker* checker = current_checker;
    Atom const* atom = checker_atom(checker, span_sv(name));
    bool container_found = false, direct = false;
    for (Scope const* scope = me; scope; scope = scope->parent) {
        ThisKind kind = scope_rare(scope)->this_ref.kind;
        if (!kind) continue;
        bool first = !container_found;
        container_found = true;
        ClassDecl const* class_decl = scope_rare(scope)->this_ref.class_decl;
        if (!class_decl || !class_decl->symbol) continue;
        direct = first;
        // (tsc's getPropertyOfType: with the members of Function and Object)
        if (ty_get_object_prop(checker, ty_static_type_of(checker, class_decl->symbol), atom)) {
            error_ctx_error_d(errs, name, 2662, "Cannot find name '", name, "'. Did you mean the static member '",
                class_decl->symbol->name, ".", name, "'?");
            return true;
        }
        if (direct && kind == THIS_CLASS_INSTANCE && ty_get_object_prop(checker, ty_declared_type_of(checker, class_decl->symbol), atom)) {
            error_ctx_error_d(errs, name, 2663, "Cannot find name '", name, "'. Did you mean the instance member 'this.", name, "'?");
            return true;
        }
    }
    return false;
}

typedef struct PendingTypeAsValue {
    Symbol const* type;
    Span name;
    ErrorCtx* errs;
} PendingTypeAsValue;

void scope_report_type_as_value(Binder* binder, Symbol const* type, Span name, bool maybe_mapped, ErrorCtx* errs) {
    if (!maybe_mapped || !type) {
        error_ctx_error_d(errs, name, 2693, "'", name, "' only refers to a type, but is being used as a value here.");
        return;
    }
    ResolveShard* shard = binder_shard(binder);
    PendingTypeAsValue** list = shard ? &shard->pending_type_as_value : &binder->pending_type_as_value;
    u32* count = shard ? &shard->pending_type_as_value_count : &binder->pending_type_as_value_count;
    *list = realloc(*list, sizeof(PendingTypeAsValue) * (*count + 1));
    panic_if(!*list, "out of memory");
    (*list)[(*count)++] = (PendingTypeAsValue){ type, name, shard ? shard_lasting_errs(shard, errs) : errs };
}

// tsc's maybeMappedType: a union of string-like and number-like types (allTypesAssignableToKind(StringOrNumberLiteral))
static bool maybe_mapped_type(Symbol const* type) {
    Checker* checker = current_checker;
    Ty const* declared = ty_declared_type_of(checker, type);
    if (declared->kind != TY_UNION) return false;
    for (u32 i = 0; i < declared->composite.types.len; i++) {
        Ty const* member = declared->composite.types.data[i];
        if (member->flags & (TYF_ANY | TYF_UNKNOWN | TYF_VOID | TYF_UNDEFINED | TYF_NULL)) return false;
        if (!ty_is_related(checker, member, ty_intrinsic(checker, TY_STRING), TY_RELATION_ASSIGNABLE) &&
            !ty_is_related(checker, member, ty_intrinsic(checker, TY_NUMBER), TY_RELATION_ASSIGNABLE)
        ) {
            return false;
        }
    }
    return true;
}

static void report_types_as_values(Binder* me) {
    for (u32 i = 0; i < my pending_type_as_value_count; i++) {
        PendingTypeAsValue const* pending = &my pending_type_as_value[i];
        Span name = pending->name;
        if (maybe_mapped_type(pending->type)) {
            bool k = csview_equals(span_sv(name), "K");
            error_ctx_error_d(pending->errs, name, 2690, "'", name, "' only refers to a type, but is being used as a value "
                "here. Did you mean to use '", k ? "P" : "K", " in ", name, "'?");
        } else {
            error_ctx_error_d(pending->errs, name, 2693, "'", name, "' only refers to a type, but is being used as a value here.");
        }
    }
    free(my pending_type_as_value);
    my pending_type_as_value = nullptr;
    my pending_type_as_value_count = 0;
}

void scope_report_missing_prefixes(Binder* me) {
    report_types_as_values(me);
    for (u32 i = 0; i < my pending_not_found_count; i++) {
        PendingNotFound const* pending = &my pending_not_found[i];
        if (report_missing_prefix(pending->scope, pending->name, pending->errs)) continue;
        ClassItem const* field = scope_invalid_initializer_field(pending->scope, span_sv(pending->name), nullptr);
        if (field) {
            scope_report_invalid_initializer_reference(field, pending->name, pending->errs);
        } else {
            report_not_found(pending->scope, pending->name, SYMBOL_VALUE, pending->shorthand, pending->errs);
        }
    }
    free(my pending_not_found);
    my pending_not_found = nullptr;
    my pending_not_found_count = my pending_not_found_cap = 0;
}

csview symbol_default_export_local_name(Symbol const* me) {
    for (u32 i = 0; i < my decl_count; i++) {
        SymbolDecl const* decl = &my decls[i];
        bool declaration = decl->kind == SYMBOL_DECL_CLASS || decl->kind == SYMBOL_DECL_FUNCTION ||
            decl->kind == SYMBOL_DECL_INTERFACE || decl->kind == SYMBOL_DECL_ENUM;
        if (declaration && span_valid(decl->name)) return span_sv(decl->name);
    }
    return (csview){0};
}

bool symbol_alias_of_unknown(Symbol const* symbol) {
    return symbol && (symbol->flags & SYMBOL_ALIAS) && symbol->alias && symbol->alias->state == ALIAS_DONE &&
        (!symbol->alias->target || symbol->alias->target->is_placeholder);
}

Symbol const* scope_unknown_alias(Scope const* me) {
    for (Scope const* scope = me; scope; scope = scope->parent) {
        SymbolTable const* tables[2] = { scope_locals(scope), nullptr };
        if (scope->kind == SCOPE_NAMESPACE && scope->container) tables[1] = symbol_members(scope->container)->exports;
        if (scope->kind == SCOPE_GLOBAL) tables[1] = &scope->binder->globals;
        for (u32 t = 0; t < 2; t++) {
            for (u32 i = 0; tables[t] && i < tables[t]->count; i++) {
                Symbol const* entry = tables[t]->entries[i].symbol;
                if (symbol_alias_of_unknown(entry)) return entry;
            }
        }
    }
    return nullptr;
}

Symbol* scope_resolve(Scope const* me, csview name, u32 meaning) {
    u64 hash = symbol_name_hash(name);
    bool from_parameters = false, from_return_type = false;
    bool arguments = (meaning & SYMBOL_VARIABLE) && csview_equals(name, "arguments");
    for (Scope const* scope = me; scope; scope = scope->parent) {
        SymbolTable const* table = scope->kind == SCOPE_GLOBAL ? &scope->binder->globals : scope_locals(scope);
        if (table->count || table->parent) {
            Symbol* found = symbol_table_lookup(table, name, hash);
            bool body_variable = from_parameters && found && (found->flags & SYMBOL_VARIABLE) && !found->is_parameter;
            // From the return type the function's own locals are visible but for its local types and its body's `var`s
            // (tsc's resolveNameHelper with lastLocation the return type node): `(): typeof groups` names the body's
            // `const groups`
            if (from_return_type && found && !found->is_parameter &&
                (((meaning & found->flags & SYMBOL_TYPE) && !(found->flags & SYMBOL_TYPE_PARAMETER)) ||
                    (meaning & found->flags & SYMBOL_FUNCTION_SCOPED_VARIABLE))
            ) {
                found = nullptr;
            }
            // (Nor are its local types in the parameter list: tsc's resolveNameHelper, "local types not visible outside
            // the function body")
            if (from_parameters && found && (meaning & found->flags & SYMBOL_TYPE) && !(found->flags & SYMBOL_TYPE_PARAMETER) &&
                !found->is_parameter
            ) {
                found = nullptr;
            }
            if (found && !body_variable && symbol_has_meaning(found, meaning)) return found;
        }
        if (scope->kind == SCOPE_GLOBAL_AUGMENTATION) {
            // A name the block declares is the merged global before the module file's locals: tsc's resolveNameHelper
            // looks it up in the augmentation's exports, whose getMergedSymbol is the global (lib.esnext.iterator's
            // `Iterator` is the global interface and `var`, not the file's local `abstract class Iterator`)
            Symbol* found = symbol_table_lookup(&scope->binder->globals, name, hash);
            bool declared_here = false;
            for (u32 i = 0; found && i < found->decl_count && !declared_here; i++) declared_here = found->decls[i].block == scope;
            if (declared_here && symbol_has_meaning(found, meaning)) return found;
        }
        if (arguments && scope->kind == SCOPE_FUNCTION && scope->function && !scope->function->is_type &&
            !function_impl_is_arrow(scope->function)
        ) {
            // A function's own `arguments` hides outer declarations (tsc's resolveNameHelper returns argumentsSymbol):
            // no symbol, see IdentifierExpr.is_arguments
            return nullptr;
        }
        from_parameters = scope->parameters && !scope->parameters_see_body;
        from_return_type = scope->return_type;
        if (scope->container && scope->container->is_module && scope->container->exports) {
            // `A` of `export default class A` (tsc's resolveName)
            Symbol* found = symbol_table_lookup(scope->container->exports, c_sv("default"), symbol_name_hash(c_sv("default")));
            // (The name first: the meaning of an alias resolves it, which may be what is being resolved)
            if (found && found->is_default_export && csview_equals_sv(symbol_default_export_local_name(found), name) &&
                symbol_has_meaning(found, meaning)
            ) {
                return found;
            }
        }
        if (scope->container && scope->container->exports) {
            Symbol* found = symbol_table_lookup(scope->container->exports, name, hash);
            // An enum and a namespace merged share their exports: an enum sees only the members, a namespace all but
            // them (tsc's resolveNameHelper looks up SymbolFlags.EnumMember, SymbolFlags.ModuleMember)
            u32 visible = scope->kind == SCOPE_ENUM ? SYMBOL_ENUM_MEMBER : ~(u32)SYMBOL_ENUM_MEMBER;
            // A CommonJS module's exports aren't names in it, except its typedefs (tsc's resolveNameHelper)
            if (found && scope->kind == SCOPE_MODULE && scope->file && scope->file->is_commonjs) {
                bool is_typedef = false;
                for (u32 i = 0; i < found->decl_count && !is_typedef; i++) {
                    is_typedef = found->decls[i].kind == SYMBOL_DECL_TYPE_ALIAS && ((TypeDecl const*)found->decls[i].node)->jsdoc;
                }
                if (!is_typedef) found = nullptr;
            }
            // `export { a }` must not find itself
            if (found && !is_export_specifier_alias(found) && symbol_has_meaning(found, meaning & visible)) return found;
        }
    }
    return nullptr;
}

ClassItem const* scope_invalid_initializer_field(Scope const* me, csview name, Symbol const* found) {
    // (Every identifier asks: skip the lookups when no scope around it has constructor locals, nearly always)
    Scope const* with_locals = me;
    while (with_locals && !scope_rare(with_locals)->constructor_local_count) with_locals = with_locals->parent;
    if (!with_locals) return nullptr;
    ClassItem const* ret = nullptr;
    for (Scope const* scope = me; scope; scope = scope->parent) {
        if (found && scope_declares_symbol(scope, found)) break;
        // (tsc keeps the outermost one when fields are nested in class expressions)
        for (u32 i = 0; i < scope_rare(scope)->constructor_local_count; i++) {
            if (csview_equals_sv(span_sv(scope_rare(scope)->constructor_locals[i]), name)) {
                ret = scope_rare(scope)->instance_field;
                break;
            }
        }
    }
    return ret;
}

void scope_report_invalid_initializer_reference(ClassItem const* field, Span name, ErrorCtx* errs) {
    Span type = field->field.has_explicit_type ? field->field.explicit_type_span : span_null;
    bool in_type = span_valid(type) && span_source(name) == span_source(type) && span_start_ptr(name) >= span_start_ptr(type) &&
        span_start_ptr(name) <= span_end_ptr(type);
    if (in_type) {
        error_ctx_error_d(errs, name, 2844, "Type of instance member variable '", field->field.name_span,
            "' cannot reference identifier '", name, "' declared in the constructor.");
    } else {
        error_ctx_error_d(errs, name, 2301, "Initializer of instance member variable '", field->field.name_span,
            "' cannot reference identifier '", name, "' declared in the constructor.");
    }
}

static Symbol const* scope_get(Scope const* me, csview name, u32 meaning, Symbol const** found) {
    Symbol* symbol = scope_resolve(me, name, meaning);
    if (found) *found = symbol;
    return symbol_for_meaning(symbol, meaning);
}

Symbol const* scope_get_value(Scope const* me, csview name, Symbol const** found) {
    Symbol* symbol = scope_resolve(me, name, SYMBOL_VALUE);
    if (found) *found = symbol;
    // (An import merged with an exported value of the module: the value, tsc's getExportSymbolOfValueSymbolIfExported
    // of the merged local)
    if (symbol && (symbol->flags & SYMBOL_ALIAS) && symbol->alias && symbol->alias->export_value) return symbol->alias->export_value;
    // An export of the block's container that this block declared a duplicate of (TS2300): that one (tsc's
    // checkIdentifier finds the block's local and takes its exportSymbol, the symbols baseline's resolveEntityName the
    // export)
    if (symbol && symbol->parent) {
        for (Scope const* scope = me; scope; scope = scope->parent) {
            if (scope->container != symbol->parent) continue;
            for (DuplicateExport const* i = scope_rare(scope)->duplicate_exports; i; i = i->next) {
                if (csview_equals_sv(i->symbol->name, name)) return i->symbol;
            }
            break;
        }
    }
    return symbol_for_meaning(symbol, SYMBOL_VALUE);
}

Symbol const* scope_get_type(Scope const* me, csview name, Symbol const** found) {
    return scope_get(me, name, SYMBOL_TYPE, found);
}

Symbol const* scope_get_namespace(Scope const* me, csview name, Symbol const** found) {
    return scope_get(me, name, SYMBOL_NAMESPACE, found);
}

void scope_note_this_reference(Scope const* me) {
    for (Scope const* scope = me; scope; scope = scope->parent) {
        // (A class field's initializer or static block is a container of its own that doesn't pass it on)
        if (scope->in_field || scope->kind == SCOPE_STATIC_BLOCK) return;
        // (Nor does an interface declaration, tsc's ContainerFlags.IsInterface)
        if (scope_rare(scope)->this_ref.kind == THIS_INTERFACE) return;
        if (scope->kind != SCOPE_FUNCTION || !scope->function) continue;
        if (function_impl_is_arrow(scope->function) || scope->function->is_type) continue;
        scope->function->contains_this = true;
        return;
    }
}

ThisRef scope_get_this(Scope const* me, bool type_else_value) {
    bool skip_class = false;
    for (Scope const* scope = me; scope; scope = scope->parent) {
        skip_class |= scope->member_decorator && !type_else_value;
        ThisKind kind = scope_rare(scope)->this_ref.kind;
        if (!kind) continue;
        if (skip_class && scope_rare(scope)->this_ref.class_decl) {
            skip_class = false;
            continue;
        }
        bool is_class = kind == THIS_CLASS_INSTANCE || kind == THIS_CLASS_STATIC || kind == THIS_INTERFACE ||
            kind == THIS_TYPE_LITERAL;
        if (!type_else_value || is_class) return scope_rare(scope)->this_ref;
        // A `this` type in a function that isn't a method of a class (tsc's getThisContainer stops there) has nothing
        // to be about
        if (scope_rare(scope)->this_ref.type_boundary) return (ThisRef){ .kind = THIS_TYPE_LITERAL };
    }
    return (ThisRef){0};
}

struct ClassDecl const* scope_enclosing_class(Scope const* me) {
    for (Scope const* scope = me; scope; scope = scope->parent) {
        if (scope_rare(scope)->this_ref.class_decl) return scope_rare(scope)->this_ref.class_decl;
    }
    return nullptr;
}

Generic const* symbol_type_generic(Symbol const* me) {
    if (!me) return nullptr;
    for (u32 i = 0; i < my decl_count; i++) {
        Generic const* generic = nullptr;
        switch (my decls[i].kind) {
            case SYMBOL_DECL_CLASS: generic = &((ClassDecl const*)my decls[i].node)->generic; break;
            case SYMBOL_DECL_INTERFACE: generic = &((InterfaceDecl const*)my decls[i].node)->generic; break;
            case SYMBOL_DECL_TYPE_ALIAS: generic = &((TypeDecl const*)my decls[i].node)->generic; break;
            default: continue;
        }
        // (A declaration without type parameters doesn't hide a later one's: `interface EventEmitter {}` and
        // `interface EventEmitter<T = DefaultEventMap>` merge into a generic interface, tsc's
        // getLocalTypeParametersOfClassOrInterfaceOrTypeAlias appends every declaration's)
        if (!generic_empty(generic)) return generic;
    }
    return nullptr;
}

// The type parameters a class, interface or type alias captures from around it (its first declaration's)
OuterGenerics const* symbol_outer_generics(Symbol const* me) {
    if (!me) return nullptr;
    for (u32 i = 0; i < my decl_count; i++) {
        switch (my decls[i].kind) {
            case SYMBOL_DECL_CLASS: return ((ClassDecl const*)my decls[i].node)->outer_generics;
            case SYMBOL_DECL_INTERFACE: return ((InterfaceDecl const*)my decls[i].node)->outer_generics;
            case SYMBOL_DECL_TYPE_ALIAS: return ((TypeDecl const*)my decls[i].node)->outer_generics;
            default: continue;
        }
    }
    return nullptr;
}

// Expandos

// Whether a declaration initialized with `init` is an expando container (tsgo's IsExpandoInitializer): a function or
// arrow function, in JavaScript also a class expression or `{}` (whose declaration has no type annotation): that
// expression, null otherwise
Expr* expando_initializer(Expr* init, bool js, bool annotated) {
    if (expr_is_perenthisized(init)) return nullptr;
    switch (init->tag) {
        case EXPR_FUNCTION: return init;
        case EXPR_CLASS_DECL: return js ? init : nullptr;
        case EXPR_OBJECT: return js && !annotated && !vec_object_expr_item_size(&init->object) ? init : nullptr;
        default: return nullptr;
    }
}

bool expando_host_is_initializer(Symbol const* host, Span* name) {
    *name = span_null;
    Expr const* init = nullptr;
    for (u32 i = 0; i < host->decl_count && !init; i++) {
        SymbolDecl const* decl = &host->decls[i];
        if (!(decl->flags & SYMBOL_VALUE)) continue;
        if (decl->kind == SYMBOL_DECL_VARIABLE) {
            Binding const* binding = decl->node;
            if (binding->parent || binding->tag != BINDING_IDENTIFIER) return false;
            init = initialized_binding_value((InitializedBinding*)binding);
        } else if (decl->kind == SYMBOL_DECL_ASSIGNMENT) {
            init = decl->node;
        } else {
            return false;
        }
    }
    if (!init || expr_is_perenthisized(init)) return false;
    if (init->tag == EXPR_FUNCTION) *name = init->function->name;
    else if (init->tag == EXPR_CLASS_DECL) *name = init->class_decl->name;
    else return false;
    return true;
}

// What a name is where an assignment to one of its properties is (tsgo's lookupName): a name in the block, then in its
// function (or file, or namespace), nowhere else
static Symbol* lookup_name_for_property_access(Scope* me, csview name) {
    u64 hash = symbol_name_hash(name);
    // (tsgo's blockScopeContainer: an `if` branch's scope of its own isn't one, the block around is)
    Scope* block = me;
    while (block->binder_transparent && block->parent) block = block->parent;
    for (Scope* scope = block; scope; scope = scope->parent) {
        if (scope != block && !scope_is_var_container(scope)) continue;
        bool global = scope->kind == SCOPE_SCRIPT || scope->kind == SCOPE_GLOBAL_AUGMENTATION;
        Symbol* found = symbol_table_lookup(global ? &my binder->globals : scope_locals(scope), name, hash);
        // (A global another file declared, which this script's declaration of the name conflicts with: the script's
        // own, tsgo's binder looks in the file's locals)
        if (found && scope->kind == SCOPE_SCRIPT && scope_rare(scope)->script_duplicates) {
            bool here = false;
            for (u32 i = 0; i < found->decl_count && !here; i++) here = span_source(found->decls[i].full) == my file;
            for (DuplicateExport const* i = scope_rare(scope)->script_duplicates; i && !here; i = i->next) {
                if (csview_equals_sv(i->symbol->name, name)) found = i->symbol;
            }
        }
        Symbol* container = scope->container;
        if (!found && container && container->exports && !container->is_global_this) {
            found = symbol_table_lookup(container->exports, name, hash);
            // `export default function f`, which is `default`
            Symbol* default_export = found ? nullptr : symbol_table_get(container->exports, c_sv("default"));
            if (default_export && default_export->is_default_export &&
                csview_equals_sv(symbol_default_export_local_name(default_export), name)
            ) {
                found = default_export;
            }
        }
        // (An import merged with an exported value of the module: the export, tsgo's lookupName takes the local's
        // exportSymbol)
        if (found && (found->flags & SYMBOL_ALIAS) && found->is_local && container && container->exports && found->alias &&
            found->alias->item && !port_item_declares_export(found->alias->item)
        ) {
            Symbol* exported = symbol_table_lookup(container->exports, name, hash);
            if (exported && exported != found && (exported->flags & SYMBOL_VALUE) && !(exported->flags & SYMBOL_ALIAS)) found = exported;
        }
        if (found || scope_is_var_container(scope)) return found;
    }
    return nullptr;
}

// Whether a symbol can have expandos (tsgo's getInitializerSymbol): its first value declaration in `file` (tsgo binds a
// file before globals merge) is a function declaration, a JavaScript class, a variable (a const outside JavaScript)
// initialized with an expando initializer, or a JavaScript assignment declaration of one (`ns.a = {}`). `init_ret`:
// that initializer
static bool symbol_is_expando_host(Symbol const* found, SourceFile const* file, bool js, Expr** init_ret) {
    *init_ret = nullptr;
    if (found->is_placeholder || (found->flags & SYMBOL_ALIAS)) return false;
    SymbolDecl const* value = nullptr;
    for (u32 i = 0; i < found->decl_count && !value; i++) {
        if ((found->decls[i].flags & SYMBOL_VALUE) && span_source(found->decls[i].full) == file) value = &found->decls[i];
    }
    if (!value) return false;
    switch (value->kind) {
        case SYMBOL_DECL_FUNCTION: return true;
        case SYMBOL_DECL_CLASS: return js;
        case SYMBOL_DECL_VARIABLE: {
            Binding const* binding = value->node;
            if (binding->parent || binding->tag != BINDING_IDENTIFIER) return false;
            if (!js && !found->is_const) return false;
            InitializedBinding* initialized = (InitializedBinding*)binding;
            *init_ret = expando_initializer(initialized_binding_value(initialized), js, binding->has_explicit_type);
            return *init_ret != nullptr;
        }
        case SYMBOL_DECL_ASSIGNMENT:
            if (!js) return false;
            // (A `/** @type {T} */ ns.a = {}` is annotated: no container, tsgo's isExpandoInitializer)
            *init_ret = expando_initializer((Expr*)value->node, js, symbol_decl_jsdoc_type(value) != nullptr);
            return *init_ret != nullptr;
        default: return false;
    }
}

// A local all of whose declarations are in `file`: no other file's resolve walk can see it
static bool expando_host_is_private(Symbol const* host, SourceFile const* file) {
    if (!host->is_local) return false;
    for (u32 i = 0; i < host->decl_count; i++) {
        if (span_source(host->decls[i].full) != file) return false;
    }
    return true;
}

// A function's or class's expandos are members of its value, like a namespace's: a declaration of no name says so
static void mark_expando_host(Symbol* host, void const* node) {
    for (u32 i = 0; i < host->decl_count; i++) {
        if (host->decls[i].kind == SYMBOL_DECL_EXPANDO) return;
    }
    // (No namespace meaning for the host: tsgo only gives it exports. The declaration says its value has the members, a
    // node that it is no namespace of a CommonJS `export =`, namespace_from_expandos_only())
    symbol_add_decl(host, (SymbolDecl){ .kind = SYMBOL_DECL_EXPANDO, .flags = SYMBOL_VALUE_MODULE, .node = node });
}

// Whether a class declares a static field or method `name` (not a computed one)
static bool class_declares_static(ClassDecl const* class_decl, Span name) {
    for_each (i, vec_class_item, &class_decl->items) {
        cstr const* item_name = i->tag == CLASS_ITEM_FIELD && !i->field.computed_key && i->field.is_static ? &i->field.name_string :
            i->tag == CLASS_ITEM_METHOD && !i->method.computed_key && i->method.is_static ? &i->method.name_string : nullptr;
        if (item_name && cstr_equals_sv(item_name, span_sv(name))) return true;
    }
    return false;
}

static void set_expando_initializer_host(Expr* init, Symbol* host) {
    if (init && init->tag == EXPR_FUNCTION) function_impl_rare_mut(init->function)->expando_host = host;
    if (init && init->tag == EXPR_CLASS_DECL) init->class_decl->expando_host = host;
    SourceFile* file = init ? (SourceFile*)span_source(init->inner_span) : nullptr;
    if (init && init->tag == EXPR_OBJECT && file) {
        // (Once per literal and binder: a file bound again by another program has a host of its own there)
        init->expando_container = true;
        for (u32 i = 0; i < file->expando_container_count; i++) {
            if (file->expando_containers[i].literal == init && file->expando_containers[i].host->binder == host->binder) {
                file->expando_containers[i].host = host;
                return;
            }
        }
        file->expando_containers = realloc(file->expando_containers, sizeof(*file->expando_containers) * (file->expando_container_count + 1));
        panic_if(!file->expando_containers, "out of memory");
        file->expando_containers[file->expando_container_count++] = (struct ExpandoContainer){ init, host };
    }
}

// What the entity name `f` or `a.b` of `f.x = v` names, if it can have expandos (tsgo's lookupEntity, then
// getInitializerSymbol): `a.b` is an expando b of the host a. `init_ret`: the host's expando initializer
static Symbol* expando_host(Scope* me, Expr const* entity, Expr** init_ret) {
    *init_ret = nullptr;
    if (expr_is_perenthisized(entity)) return nullptr;
    SourceFile const* file = my file;
    bool js = file && file->is_js;
    Symbol* found = nullptr;
    if (entity->tag == EXPR_PROPERTY) {
        // (Outside JavaScript only a name: tsgo's IsEntityNameExpressionEx without allowJS)
        Span name = expr_static_property_name(entity);
        if (!js || !span_valid(name)) return nullptr;
        Expr* ignored;
        Symbol* object = expando_host(me, &entity->property->expr, &ignored);
        found = object && object->exports ? symbol_table_get(object->exports, span_sv(name)) : nullptr;
    } else if (entity->tag == EXPR_IDENTIFIER && !entity->identifier.keyword) {
        found = lookup_name_for_property_access(me, span_sv(entity->inner_span));
        // The local `f` of `export default function f` is the default export
        if (found && symbol_rare(found)->export_symbol) found = symbol_rare(found)->export_symbol;
    }
    return found && symbol_is_expando_host(found, file, js, init_ret) ? found : nullptr;
}

// The name an assignment declaration `o.x = v` / `o["x"] = v` is written with: a string element access is named with
// its quotes (tsc's getNameOfSymbolAsWritten takes the declaration's name), which the symbols baseline prints as `o["x"]`
static Span written_member_name(Expr const* left, Span name) {
    // (`Object.defineProperty(o, "x", d)`: the key literal as written, quotes kept, `o['x']`)
    if (left->tag == EXPR_CALL && vec_expr_size(&left->call->args) == 3) {
        Expr const* key = vec_expr_at(&left->call->args, 1);
        if (key->tag == EXPR_LITERAL && key->literal.tag == VALUE_STRING) return key->inner_span;
    }
    if (left->tag == EXPR_PROPERTY && left->property->form == PROPERTY_EXPR_BRACKET &&
        left->property->index.tag == EXPR_LITERAL && left->property->index.literal.tag == VALUE_STRING
    ) {
        return left->property->index.inner_span;
    }
    return name;
}

static void declare_assignment_member(
    Scope* me, SymbolTable** table, Symbol* parent, Span name, Expr* left, Expr* value, TypeNode const* jsdoc_type,
    AssignmentDeclKind kind, uint8_t jsdoc_modifiers
);

// `f.x = v` (tsgo's bindExpandoPropertyAssignment, bound by bindDeferredExpandoAssignment)
static void add_expando_decl(Scope* me, Expr* left, Expr* right, TypeNode const* jsdoc_type);
static void add_expando_member(
    Scope* me, Expr* object, Span name, Expr* left, Expr* right, TypeNode const* jsdoc_type, Expr const* late_bound_key,
    AssignmentDeclKind kind
);

static void add_expando(Scope* me, BinaryOpExpr* op) {
    if (op->op != BINARY_OP_EQUAL) return;
    op->expando_bound = true;
    ResolveShard* shard = js_shard(me);
    if (shard) {
        DeferredEvent* event = defer_event(shard, DEFERRED_JS_EXPANDO, me, nullptr);
        event->op = op;
        event->link = walk_unvisited(&op->left);
        return;
    }
    add_expando_decl(me, &op->left, &op->right, op->jsdoc_type);
}

static void add_expando_decl(Scope* me, Expr* left, Expr* right, TypeNode const* jsdoc_type) {
    if (left->tag != EXPR_PROPERTY) return;
    // `module.exports = v` in JavaScript is the module's `export =` whatever `module` is here, never a member of a local
    // `module` (tsc's getAssignmentDeclarationKind: ModuleExports)
    if (my file && my file->is_js && expr_is_module_exports(left)) return;
    Span name = expr_static_property_name(left);
    Expr* object = &left->property->expr;
    // `f[k] = v` with an entity name k: a late bound member of a function or class (tsc's
    // isBindableStaticElementAccessExpression, addLateBoundAssignmentDeclarationToSymbol). Named `[k]` as written
    Expr const* late_bound_key = nullptr;
    if (!span_valid(name) && left->property->form == PROPERTY_EXPR_BRACKET && !left->property->optional_chain &&
        !expr_is_perenthisized(left) && expr_is_entity_name(&left->property->index)
    ) {
        late_bound_key = &left->property->index;
        name = span_make_in(expr_outer_span(left), span_end_ptr(expr_outer_span(object)), span_end_ptr(expr_outer_span(left)));
        if (!span_valid(name) || !csview_starts_with(span_sv(name), "[")) return;
    }
    if (!span_valid(name)) return;
    if (late_bound_key && !expr_is_entity_name(object)) return;
    add_expando_member(me, object, name, left, right, jsdoc_type, late_bound_key, ASSIGNMENT_DECL_PROPERTY);
}

// `Object.defineProperty(o, "x", {...})` of a container o (tsgo's JSDeclarationKindObjectDefinePropertyValue): declared
// by the call
static void add_define_property(Scope* me, Expr* call) {
    Span name;
    if (!expr_is_define_property_call(call, &name)) return;
    ResolveShard* shard = js_shard(me);
    if (shard) {
        DeferredEvent* event = defer_event(shard, DEFERRED_JS_DEFINE_PROPERTY, me, nullptr);
        event->left = call;
        return;
    }
    Expr* object = (Expr*)vec_expr_at(&call->call->args, 0);
    if (expr_is_exports_object(object)) return; // (add_define_property_export())
    add_expando_member(me, object, name, call, call, nullptr, nullptr, ASSIGNMENT_DECL_DEFINE_PROPERTY);
}

// Member `name` of what `object` names, declared by `left` (`object.name`, or a call) = `right`
static void add_expando_member(
    Scope* me, Expr* object, Span name, Expr* left, Expr* right, TypeNode const* jsdoc_type, Expr const* late_bound_key,
    AssignmentDeclKind kind
) {
    // A private name is no expando (tsc's isBindableStaticNameExpression)
    if (name.size && name.buf[0] == '#') return;
    Expr* init;
    Symbol* host = expando_host(me, object, &init);
    if (!host) return;
    ResolveShard* shard = binder_shard(my binder);
    if (shard && !expando_host_is_private(host, my file)) {
        // Other files may read it meanwhile: in program order instead (plan 45)
        DeferredEvent* event = defer_event(shard, DEFERRED_EXPANDO, me, nullptr);
        event->object = object;
        event->member = name;
        event->left = left;
        event->right = right;
        event->jsdoc_type = jsdoc_type;
        event->late_bound_key = late_bound_key;
        event->assignment = kind;
        event->link = walk_unvisited(left);
        return;
    }
    set_expando_initializer_host(init, host);
    symbol_ensure_exports(host);
    if (!init) mark_expando_host(host, object);
    // Only when nothing but expando assignments declares the name (tsgo's bindDeferredExpandoAssignment: a namespace's or
    // class's member of that name is what the assignment assigns; its binder sees the file's own declarations, another
    // file's merge later)
    // (A class's exports have its `prototype` in tsgo)
    if (span_equals(name, "prototype") && (host->flags & SYMBOL_CLASS)) return;
    Symbol const* existing = symbol_table_get(host->exports, span_sv(name));
    // (A `{}` container's expandos are the object literal's members in tsgo, a table of their own: the namespace of a
    // dotted `@typedef {T} ns.a.B` is in the variable's, which doesn't stop `ns.a = {}`)
    bool literal = init && init->tag == EXPR_OBJECT;
    for (u32 i = 0; existing && i < existing->decl_count; i++) {
        if (literal && is_delayed_jsdoc_decl(&existing->decls[i])) continue;
        if (existing->decls[i].kind != SYMBOL_DECL_ASSIGNMENT && span_source(existing->decls[i].full) == my file) return;
    }
    // (A class's static members are its exports in tsgo, here they're its items: a class declaration's, or those of the
    // class expression the host is initialized with, `ns.sub = class { static cb; }`, whose symbol tsgo's
    // getInitializerSymbol declares in)
    for (u32 d = 0; d < host->decl_count; d++) {
        if (host->decls[d].kind != SYMBOL_DECL_CLASS || span_source(host->decls[d].full) != my file) continue;
        if (class_declares_static(host->decls[d].node, name)) return;
    }
    if (init && init->tag == EXPR_CLASS_DECL && class_declares_static(init->class_decl, name)) return;
    bool merged;
    Symbol* member = declare_symbol(me, host->exports, span_sv(name), name, SYMBOL_PROPERTY, 0, false, &merged);
    if (literal) symbol_rare_mut(member)->literal_member = true;
    if (!merged) {
        member->name = span_sv(name);
        member->name_span = written_member_name(left, name);
        // (A `{}` container's are the object literal's members in tsgo, whose symbol has no name to qualify them with)
        member->parent = init && init->tag == EXPR_OBJECT ? nullptr : host;
        member->exported = true;
    }
    Expr* value = right;
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    if (late_bound_key && !symbol_rare(member)->late_bound_key) symbol_rare_mut(member)->late_bound_key = late_bound_key;
    if (left->tag == EXPR_PROPERTY && (member || left->property->rare)) property_rare_mut(left->property)->declares = member;
    if (left->tag == EXPR_CALL && (member || left->call->rare)) call_rare_mut(left->call)->declares = member;
    symbol_add_decl(member, (SymbolDecl){
        .kind = SYMBOL_DECL_ASSIGNMENT, .assignment = kind, .flags = SYMBOL_PROPERTY, .node = value,
        .name = expr_outer_span(left), .full = expr_outer_span(left), .js = symbol_decl_js(my binder, jsdoc_type, true),
    });
}

static void add_function_export_member(Scope* me, Expr* expr, bool export_equals) {
    Symbol* host = my function ? my function->symbol : nullptr;
    if (!host) return;
    csview name = export_equals ? c_sv("export=") : c_sv("default");
    symbol_ensure_exports(host);
    mark_expando_host(host, expr);
    bool merged;
    Symbol* member = declare_symbol(me, host->exports, name, expr_outer_span(expr), SYMBOL_PROPERTY, 0, false, &merged);
    if (!merged) {
        member->name = name;
        member->name_span = span_null;
        member->parent = host;
        member->exported = true;
    }
    symbol_add_decl(member, (SymbolDecl){
        .kind = SYMBOL_DECL_EXPR, .flags = SYMBOL_PROPERTY, .node = expr, .name = expr_outer_span(expr),
        .full = expr_outer_span(expr),
    });
}

static void declare_function_export(Scope* me, Expr* expr, bool export_equals) {
    Symbol* host = my function ? my function->symbol : nullptr;
    if (!host || !(host->flags & SYMBOL_FUNCTION) || expr->tag == EXPR_IMPLICIT_ANY) return;
    // (An alias that means no value isn't a member: a namespace without values, a type)
    Expr const* first = expr;
    while (first->tag == EXPR_PROPERTY && !expr_is_perenthisized(first)) first = &first->property->expr;
    bool alias = first->tag == EXPR_IDENTIFIER && !first->identifier.keyword && !expr_is_perenthisized(first) &&
        (expr == first || is_unparenthesized_entity_name(expr));
    if (alias && !scope_resolve(me, span_sv(first->inner_span), SYMBOL_VALUE)) return;
    ResolveShard* shard = binder_shard(my binder);
    if (shard && !expando_host_is_private(host, my file)) {
        // (Other files may read the function's members meanwhile: in program order, like an expando)
        DeferredEvent* event = defer_event(shard, DEFERRED_FUNCTION_EXPORT, me, nullptr);
        event->right = expr;
        event->namespace = export_equals;
        return;
    }
    add_function_export_member(me, expr, export_equals);
}

// Every assignment of a chain (`f.a = f.b = 1`)
static void add_expando_chain(Scope* me, Expr* e) {
    for (; e->tag == EXPR_BINARY_OP && e->binary_op->op == BINARY_OP_EQUAL; e = &e->binary_op->right) {
        if (expr_is_perenthisized(e)) break;
        add_expando(me, e->binary_op);
    }
}

void scope_add_nested_expando(Scope* me, BinaryOpExpr* op) {
    if (op->expando_bound) return;
    // In an object literal or a class's field initializer, tsc's binder looks the name up in the literal or the class
    // (its container), where it doesn't find a variable (lookupSymbolForName). (A class's statics would be found)
    FlowBuilder const* flow = my flow;
    if (flow && (flow->object_literal_depth || flow->initializer_context == INITIALIZER_INSTANCE_FIELD ||
        flow->initializer_context == INITIALIZER_STATIC_FIELD)
    ) {
        return;
    }
    add_expando(me, op);
}

// The assignments of a block's expression statements and variable initializers are declared with the block's names
// (tsgo binds them all once the file is bound); other ones when the resolve walk meets them (scope_add_nested_expando())
void scope_add_expandos(Scope* me, Block const* block) {
    for_each (i, vec_statement, &block->statements) {
        if (i->tag == STATEMENT_EXPR) {
            add_expando_chain(me, &i->expr);
            if (my file && my file->is_js) add_define_property(me, &i->expr);
        }
        // `var a = f.x = {}`
        if (i->tag == STATEMENT_DECL_VARIABLE) {
            for_each (j, vec_initialized_binding, &i->decl.variable.initializers) add_expando_chain(me, initialized_binding_value(j));
        }
    }
}

// JavaScript classes

// Declares an assignment member `name` (`left` = v) in a class's or object literal's table
static void declare_assignment_member(
    Scope* me, SymbolTable** table, Symbol* parent, Span name, Expr* left, Expr* value, TypeNode const* jsdoc_type,
    AssignmentDeclKind kind, uint8_t jsdoc_modifiers
) {
    if (!*table) *table = arena_new(binder_arena(my binder), SymbolTable);
    bool merged;
    Symbol* member = declare_symbol(me, *table, span_sv(name), name, SYMBOL_PROPERTY, 0, false, &merged);
    if (!merged) {
        member->name = span_sv(name);
        member->name_span = written_member_name(left, name);
        member->parent = parent;
        member->exported = true;
    }
    if (left->tag == EXPR_PROPERTY && (member || left->property->rare)) property_rare_mut(left->property)->declares = member;
    if (left->tag == EXPR_CALL && (member || left->call->rare)) call_rare_mut(left->call)->declares = member;
    symbol_add_decl(member, (SymbolDecl){
        .kind = SYMBOL_DECL_ASSIGNMENT, .assignment = kind, .flags = SYMBOL_PROPERTY, .node = value,
        .name = expr_outer_span(left), .full = expr_outer_span(left), .js = symbol_decl_js(my binder, jsdoc_type, true), .jsdoc_modifiers = jsdoc_modifiers,
    });
}

void scope_note_ts_this_assignment(Scope const* me, Expr const* left) {
    if (left->tag != EXPR_PROPERTY) return;
    Span name = expr_static_property_name(left);
    PropertyExpr const* access = left->property;
    Expr const* prop = access->form == PROPERTY_EXPR_BRACKET ? &access->index : nullptr;
    if (!span_valid(name) && prop && prop->tag == EXPR_LITERAL && prop->literal.tag == VALUE_NUMBER) name = prop->inner_span; // `this[0]`
    Expr const* object = &left->property->expr;
    if (object->tag != EXPR_IDENTIFIER || object->identifier.keyword != KEYWORD_IDENTIFIER_THIS) return;
    // Directly in the constructor or a static block: not in a nested function, arrow functions included
    Scope const* scope = me;
    while (scope && scope->kind != SCOPE_FUNCTION && scope->kind != SCOPE_STATIC_BLOCK) scope = scope->parent;
    if (!scope) return;
    bool is_static = object->identifier.this_kind == THIS_CLASS_STATIC;
    if (is_static ? scope->kind != SCOPE_STATIC_BLOCK :
        (object->identifier.this_kind != THIS_CLASS_INSTANCE || !scope->function || !scope->function->is_constructor)
    ) {
        return;
    }
    Symbol const* class_symbol = object->identifier.target;
    ClassDecl const* class_decl = class_symbol ? symbol_find_node(class_symbol, SYMBOL_DECL_CLASS) : nullptr;
    if (!class_decl) return;
    ClassItem* field = span_valid(name) ? class_decl_find_member(class_decl, name_cooked(span_sv(name)), is_static) : nullptr;
    // `this[k] = v` of a field `[k]: T`, written the same way
    for_each (i, vec_class_item, &class_decl->items) {
        if (field) break;
        if (i->tag == CLASS_ITEM_FIELD && i->field.computed_key && i->field.is_static == is_static &&
            csview_equals_sv(span_sv(expr_outer_span(i->field.computed_key)), span_sv(property_key_span(access)))
        ) {
            field = (ClassItem*)i;
        }
    }
    if (!field && !is_static && prop &&
        (prop->tag == EXPR_IDENTIFIER || prop->tag == EXPR_PROPERTY)
    ) {
        vec_expr_cptr_push(&((ClassDecl*)class_decl)->computed_this_assignments, left);
    }
    if (field && field->tag == CLASS_ITEM_FIELD && !field->field.ts_assigned) {
        field->field.ts_assigned = left;
        field->field.ts_assigned_class = class_decl;
    }
}

static void add_this_property(Scope* me, Expr* left, Expr* value, TypeNode const* jsdoc_type, uint8_t jsdoc_modifiers);

void scope_add_this_property(Scope* me, BinaryOpExpr* op) {
    Expr* value = &op->right;
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    add_this_property(me, &op->left, value, op->jsdoc_type, op->jsdoc_modifiers);
}

static void add_this_property(Scope* me, Expr* left, Expr* value, TypeNode const* jsdoc_type, uint8_t jsdoc_modifiers) {
    if (left->tag != EXPR_PROPERTY) return;
    Span name = expr_static_property_name(left);
    Expr const* object = &left->property->expr;
    // (`this[k]` with an entity name k declares no instance member: tsgo's bindThisPropertyAssignment gives the late
    // bound name to the class's exports, which getMembersOfSymbol only reads for its static side)
    // (Private names must be declared, even in JavaScript)
    if (!span_valid(name) || csview_starts_with(span_sv(name), "#")) return;
    IdentifierExpr const* this = expr_this_keyword(object);
    if (!this) return;
    ResolveShard* shard = js_shard(me);
    if (shard) {
        DeferredEvent* event = defer_event(shard, DEFERRED_JS_THIS_PROPERTY, me, nullptr);
        event->left = left;
        event->right = value;
        event->jsdoc_type = jsdoc_type;
        event->jsdoc_modifiers = jsdoc_modifiers;
        // (The walk has resolved `left` already: the replay links the declaration)
        event->link = true;
        return;
    }
    // The function `this` belongs to (arrow functions have their container's)
    FunctionImpl const* function = nullptr;
    for (Scope const* scope = me; scope && !function; scope = scope->parent) {
        if (scope->kind == SCOPE_FUNCTION && scope->function && !function_impl_is_arrow(scope->function)) function = scope->function;
        if (scope->kind == SCOPE_STATIC_BLOCK || scope->kind == SCOPE_MODULE || scope->kind == SCOPE_SCRIPT) break;
    }
    switch (this->this_kind) {
        case THIS_CLASS_INSTANCE: {
            Symbol* class_symbol = (Symbol*)this->target;
            if (!class_symbol) return;
            AssignmentDeclKind kind = function && function->is_constructor ?
                ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR : ASSIGNMENT_DECL_THIS_IN_METHOD;
            declare_assignment_member(me, &symbol_rare_mut(class_symbol)->members, class_symbol, name, left, value, jsdoc_type, kind, jsdoc_modifiers);
            if (left->property->context) expr_link_assignment_declaration(left, my binder);
            Symbol* member = (Symbol*)symbol_table_get(symbol_rare(class_symbol)->members, span_sv(name));
            if (kind == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR && member && !symbol_rare(member)->constructor_assigned) {
                symbol_rare_mut(member)->constructor_assigned = left;
                symbol_rare_mut(member)->declaring_constructor = function;
            }
            // A field the class body declares without a type or initializer has the type the constructor assigns
            ClassDecl const* class_decl = symbol_find_node(class_symbol, SYMBOL_DECL_CLASS);
            ClassItem* field = class_decl ? class_decl_find_member(class_decl, span_sv(name), false) : nullptr;
            if (kind == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR && field && field->tag == CLASS_ITEM_FIELD &&
                !field->field.has_explicit_type && field->field.value.tag == EXPR_IMPLICIT_ANY
            ) {
                field->field.js_assigned = symbol_table_get(symbol_rare(class_symbol)->members, span_sv(name));
            }
        }   break;
        case THIS_CLASS_STATIC: {
            Symbol* class_symbol = (Symbol*)this->target;
            if (!class_symbol) return;
            // (A static the class body declares is the symbol: tsgo's declareSymbolEx finds it in the exports)
            ClassDecl const* static_class = symbol_find_node(class_symbol, SYMBOL_DECL_CLASS);
            if (static_class && class_decl_find_member(static_class, span_sv(name), true)) return;
            symbol_ensure_exports(class_symbol);
            mark_expando_host(class_symbol, left);
            declare_assignment_member(me, &class_symbol->exports, class_symbol, name, left, value, jsdoc_type, ASSIGNMENT_DECL_THIS_IN_METHOD, jsdoc_modifiers);
            if (left->property->context) expr_link_assignment_declaration(left, my binder);
        }   break;
        default: break;
    }
}

// Plan 45

void resolve_shard_merge(ResolveShard* me) {
    check(resolve_shard == nullptr);
    Binder* binder = my binder;
    ErrorCtx* errs = binder->errs;
    ErrorCtx* discarded = binder_discarded_errs(binder);
    // (What the shard's pending reports go to once its contexts are merged)
    #define MERGED_ERRS(e) ((e) == &my errs ? errs : (e) == &my discarded ? discarded : (e))
    // (Everything replays in walk order)
    for (u32 i = 0; i < my deferred_count; i++) {
        DeferredEvent const* event = &my deferred[i];
        ErrorCtx* event_errs = event->errs;
        switch (event->kind) {
            case DEFERRED_NOT_FOUND:
                report_not_found(event->scope, event->at, event->meaning, event->shorthand, event_errs);
                break;
            case DEFERRED_MISSPELLED:
                if (!report_misspelled(event->scope, event->at, event->name, event->meaning, event->namespace, event_errs)) {
                    report_misspelled_fallback(event->at, event->name, event->fallback, event_errs);
                }
                break;
            case DEFERRED_EXPANDO:
                add_expando_member(event->scope, event->object, event->member, event->left, event->right, event->jsdoc_type,
                    event->late_bound_key, event->assignment);
                if (event->link) expr_link_assignment_declaration(event->left, binder);
                break;
            case DEFERRED_JS_EXPANDO:
                add_expando(event->scope, event->op);
                if (event->link) expr_link_assignment_declaration(&event->op->left, binder);
                break;
            case DEFERRED_JS_DEFINE_PROPERTY:
                add_define_property(event->scope, event->left);
                break;
            case DEFERRED_JS_THIS_PROPERTY:
                add_this_property(event->scope, event->left, event->right, event->jsdoc_type, event->jsdoc_modifiers);
                if (event->link) expr_link_assignment_declaration(event->left, binder);
                break;
            case DEFERRED_FUNCTION_EXPORT:
                add_function_export_member(event->scope, event->right, event->namespace);
                break;
        }
    }
    for (u32 i = 0; i < my symbol_count; i++) {
        *(Symbol**)list_push((void**)&binder->symbols, &binder->symbol_count, &binder->symbol_cap, sizeof(Symbol*)) = my symbols[i];
    }
    for (u32 i = 0; i < my pending_not_found_count; i++) {
        PendingNotFound pending = my pending_not_found[i];
        pending.errs = MERGED_ERRS(pending.errs);
        *(PendingNotFound*)list_push((void**)&binder->pending_not_found, &binder->pending_not_found_count,
            &binder->pending_not_found_cap, sizeof(PendingNotFound)) = pending;
    }
    if (my pending_type_as_value_count) {
        u32 count = binder->pending_type_as_value_count + my pending_type_as_value_count;
        binder->pending_type_as_value = realloc(binder->pending_type_as_value, sizeof(PendingTypeAsValue) * count);
        panic_if(!binder->pending_type_as_value, "out of memory");
        for (u32 i = 0; i < my pending_type_as_value_count; i++) {
            PendingTypeAsValue pending = my pending_type_as_value[i];
            pending.errs = MERGED_ERRS(pending.errs);
            binder->pending_type_as_value[binder->pending_type_as_value_count++] = pending;
        }
    }
    for (u32 i = 0; i < my pending_export_merge_count; i++) {
        *(PendingExportMerge*)list_push((void**)&binder->pending_export_merges, &binder->pending_export_merge_count,
            &binder->pending_export_merge_cap, sizeof(PendingExportMerge)) = my pending_export_merges[i];
    }
    #undef MERGED_ERRS
    error_ctx_merge(errs, &my errs);
    error_ctx_merge(discarded, &my discarded);
    error_ctx_drop(&my errs);
    error_ctx_drop(&my discarded);
    free(my deferred);
    free(my symbols);
    free(my pending_not_found);
    free(my pending_type_as_value);
    free(my pending_export_merges);
    *me = (ResolveShard){0};
}
