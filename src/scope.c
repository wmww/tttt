#include "scope.h"
#include "several_types.h"
#include "error.h"
#include "source_file.h"
#include "generic.h"
#include "program.h"
#include "port_statement.h"
#include "function_impl.h"
#include "class_decl.h"
#include "interface_decl.h"
#include <math.h>
#include "ty/ty.h"
#include "lib_features.h"
#include "statement.h"
#include "enum_decl.h"
#include "unused.h"

// Symbol tables

#define SYMBOL_TABLE_SCAN_MAX 8

u64 symbol_name_hash(csview name) {
    u64 h = 0xcbf29ce484222325ull;
    for (isize i = 0; i < name.size; i++) {
        h ^= (unsigned char)name.buf[i];
        h *= 0x100000001b3ull;
    }
    return h ^ (h >> 32);
}

static inline bool entry_matches(SymbolEntry const* entry, csview name, u64 hash) {
    return entry->hash == hash && entry->name.size == name.size && !memcmp(entry->name.buf, name.buf, (size_t)name.size);
}

static Symbol* symbol_table_get_own(SymbolTable const* me, csview name, u64 hash) {
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
    if (my count == my cap) {
        u32 cap = my cap ? my cap * 2 : 4;
        SymbolEntry* entries = arena_new_n(arena, SymbolEntry, cap);
        if (my count) memcpy(entries, my entries, sizeof(SymbolEntry) * my count);
        my entries = entries;
        my cap = cap;
    }
    my entries[my count++] = (SymbolEntry){ .hash = hash, .name = name, .symbol = symbol };
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
            shadowed = symbol_table_get_own(above, my entries[i].name, my entries[i].hash);
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
    *my global_scope = (Scope){ .kind = SCOPE_GLOBAL, .binder = me, .errs = errs };
    return me;
}

ErrorCtx* binder_discarded_errs(Binder* me) {
    if (!my discarded_errs) my discarded_errs = c_new(ErrorCtx, error_ctx_init());
    return my discarded_errs;
}

void binder_destroy(Binder* me) {
    if (!me) return;
    free(my symbols);
    free(my pattern_modules);
    free(my augmentations);
    free(my pending_not_found);
    if (my discarded_errs) {
        error_ctx_drop(my discarded_errs);
        free(my discarded_errs);
    }
    free(my global_expandos);
    free(my overlays.keys);
    free(my overlays.values);
    arena_drop(&my arena);
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
    Symbol* ret = arena_new(&my arena, Symbol);
    ret->name = name;
    ret->flags = flags;
    ret->binder = me;
    if (flags & SYMBOL_ALIAS) ret->alias = arena_new(&my arena, SymbolAlias);
    if (my symbol_count == my symbol_cap) {
        my symbol_cap = my symbol_cap ? my symbol_cap * 2 : 64;
        my symbols = realloc(my symbols, sizeof(Symbol*) * my symbol_cap);
    }
    my symbols[my symbol_count++] = ret;
    return ret;
}

static void symbol_ensure_exports(Symbol* me) {
    if (!my exports) my exports = arena_new(&my binder->arena, SymbolTable);
}

Symbol* binder_new_module_symbol(Binder* me, SourceFile* file) {
    Symbol* ret = binder_new_symbol(me, c_sv(""), SYMBOL_VALUE_MODULE);
    ret->is_module = true;
    ret->module = arena_new(&my arena, SymbolModule);
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
    symbol_table_insert(&my globals, &my arena, name, symbol_name_hash(name), symbol);
}

static void symbol_add_decl(Symbol* me, SymbolDecl decl);

void binder_add_module_declaration(Symbol* module, NamespaceDecl const* decl) {
    symbol_add_decl(module, (SymbolDecl){
        .kind = SYMBOL_DECL_NAMESPACE, .flags = SYMBOL_VALUE_MODULE, .node = decl, .name = decl->name, .full = decl->full_span,
    });
}

Symbol* binder_ambient_module(Binder* me, csview name, Span name_span) {
    u64 hash = symbol_name_hash(name);
    // TODO: one of the shared lib program would want an overlay like the globals (no lib declares any)
    Symbol* ret = symbol_table_lookup(&my ambient_modules, name, hash);
    if (ret) return ret;
    ret = binder_new_symbol(me, name_span.sv, SYMBOL_VALUE_MODULE);
    ret->is_module = true;
    ret->module = arena_new(&my arena, SymbolModule);
    ret->name_span = name_span;
    symbol_ensure_exports(ret);
    symbol_table_insert(&my ambient_modules, &my arena, name, hash, ret);
    if (memchr(name.buf, '*', (size_t)name.size)) {
        if (my pattern_module_count == my pattern_module_cap) {
            my pattern_module_cap = my pattern_module_cap ? my pattern_module_cap * 2 : 4;
            my pattern_modules = realloc(my pattern_modules, sizeof(Symbol*) * my pattern_module_cap);
        }
        my pattern_modules[my pattern_module_count++] = ret;
    }
    return ret;
}

static void symbol_add_decl(Symbol* me, SymbolDecl decl) {
    if (my decl_count == my decl_cap) {
        u32 cap = my decl_cap ? my decl_cap * 2 : 1; // Most symbols have one declaration
        SymbolDecl* decls = arena_new_n(&my binder->arena, SymbolDecl, cap);
        if (my decl_count) memcpy(decls, my decls, sizeof(SymbolDecl) * my decl_count);
        my decls = decls;
        my decl_cap = cap;
    }
    my decls[my decl_count++] = decl;
}

// Stands for something that couldn't be resolved: has every meaning, and everything about it is any
static Symbol* binder_new_placeholder(Binder* me, Span name, Span full) {
    Symbol* ret = binder_new_symbol(
        me, name.sv, SYMBOL_FUNCTION_SCOPED_VARIABLE | SYMBOL_TYPE_ALIAS | SYMBOL_NAMESPACE_MODULE
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
    // In tsc's order: its binder binds the function declarations of a block before its other statements
    // (bindEachFunctionsFirst). Declarations of the same block are grouped (the file, without a block)
    #define same_block(a, b) ((a).block && (b).block ? (a).block == (b).block : (a).full.source == (b).full.source)
    for (u32 i = 0; i < my decl_count; i++) {
        bool seen = false;
        for (u32 j = 0; j < i; j++) seen |= same_block(my decls[j], my decls[i]);
        if (seen) continue;
        for (u32 pass = 0; pass < 2; pass++) {
            for (u32 j = i; j < my decl_count; j++) {
                if (!same_block(my decls[j], my decls[i]) || !span_valid(my decls[j].full)) continue;
                if ((my decls[j].kind == SYMBOL_DECL_FUNCTION) == (pass == 0)) vec_span_push(&ret, my decls[j].full);
            }
        }
    }
    #undef same_block
    return ret;
}

// Scopes

Scope* scope_new(Scope* parent, ScopeKind kind) {
    Scope* ret = arena_new(&parent->binder->arena, Scope);
    ret->parent = parent;
    ret->kind = kind;
    ret->binder = parent->binder;
    ret->errs = parent->errs;
    ret->ambient = parent->ambient;
    ret->flow = parent->flow;
    ret->file = parent->file;
    ret->strict = parent->strict;
    ret->generics = parent->generics;
    if (kind == SCOPE_BLOCK) ret->function = parent->function;
    return ret;
}

void scope_reparent(Scope* me, Scope* parent) {
    my parent = parent;
    my errs = parent->errs;
    my ambient = parent->ambient;
    my flow = parent->flow;
    my file = parent->file;
    my strict = parent->strict;
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
        csview text = i->expr.inner_span.sv;
        if (csview_equals(text, "\"use strict\"") || csview_equals(text, "'use strict'")) return true;
    }
    return false;
}

// 2: in a class, 1: in a module (which says why the code is strict), else 0
static int strict_reason(Scope const* me) {
    for (Scope const* scope = me; scope; scope = scope->parent) {
        if (scope->this_ref.class_decl) return 2;
    }
    return my file && my file->is_module ? 1 : 0;
}

void scope_check_strict_identifier(Scope const* me, Span name, bool in_type) {
    if (my ambient || !span_valid(name) || !name.source || name.source->has_parse_errors) return;
    static char const* const reserved[] = { "implements", "interface", "let", "package", "private", "protected", "public",
        "static", "yield" };
    bool is_reserved = false;
    for (u32 i = 0; i < c_arraylen(reserved) && !is_reserved; i++) is_reserved = csview_equals(name.sv, reserved[i]);
    if (!my strict || !is_reserved) {
        // (tsc's checkContextualIdentifier: `await` and `yield` where they are operators)
        // (Not in types: the parser has no await or yield context there)
        bool await = !in_type && csview_equals(name.sv, "await"), yield = !in_type && csview_equals(name.sv, "yield");
        // The await/yield context: of the nearest function, a static block (await), nothing past a class boundary
        // (member initializers are an implicit function)
        // (A function expression's name is in its function's context as far as that is async or a generator, and in
        // the outer one otherwise)
        bool await_context = false, yield_context = false, top_level = true;
        for (Scope const* scope = me; scope; scope = scope->parent) {
            if (scope->expression_name_of) {
                await_context |= scope->expression_name_of->is_async;
                yield_context |= scope->expression_name_of->is_generator;
                continue;
            }
            if (scope->kind == SCOPE_FUNCTION && scope->function) {
                await_context |= scope->function->is_async;
                yield_context |= scope->function->is_generator;
            } else if (scope->kind == SCOPE_STATIC_BLOCK) {
                await_context = true;
            } else if (!scope->this_ref.class_decl) {
                continue;
            }
            top_level = false;
            break;
        }
        if (await && my file && my file->is_module && top_level && !await_context) {
            error_ctx_error_d(my errs, name, 1262, "Identifier expected. '", name, "' is a reserved word at the top-level of a module.");
        } else if ((await && await_context) || (yield && yield_context)) {
            error_ctx_error_d(my errs, name, 1359, "Identifier expected. '", name, "' is a reserved word that cannot be used here.");
        }
        return;
    }
    switch (strict_reason(me)) {
        case 2:
            error_ctx_error_d(my errs, name, 1213, "Identifier expected. '", name, "' is a reserved word in strict mode. "
                "Class definitions are automatically in strict mode.");
            break;
        case 1:
            error_ctx_error_d(my errs, name, 1214, "Identifier expected. '", name, "' is a reserved word in strict mode. "
                "Modules are automatically in strict mode.");
            break;
        default:
            error_ctx_error_d(my errs, name, 1212, "Identifier expected. '", name, "' is a reserved word in strict mode.");
    }
}

void scope_check_strict_eval_or_arguments(Scope const* me, Span name) {
    if (!my strict || !span_valid(name) || (!csview_equals(name.sv, "eval") && !csview_equals(name.sv, "arguments"))) return;
    switch (strict_reason(me)) {
        case 2:
            error_ctx_error_d(my errs, name, 1210, "Code contained in a class is evaluated in JavaScript's strict mode which "
                "does not allow this use of '", name, "'. For more information, see "
                "https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Strict_mode.");
            break;
        case 1:
            error_ctx_error_d(my errs, name, 1215, "Invalid use of '", name, "'. Modules are automatically in strict mode.");
            break;
        default:
            error_ctx_error_d(my errs, name, 1100, "Invalid use of '", name, "' in strict mode.");
    }
}

void scope_check_strict_block_function(Scope const* me, Span name) {
    if (!my strict || my ambient || my kind != SCOPE_BLOCK || !span_valid(name)) return;
    if (compiler_options()->target >= SCRIPT_TARGET_ES2015) return;
    switch (strict_reason(me)) {
        case 2:
            error_ctx_error_d(my errs, name, 1251, "Function declarations are not allowed inside blocks in strict mode when "
                "targeting 'ES5'. Class definitions are automatically in strict mode.");
            break;
        case 1:
            error_ctx_error_d(my errs, name, 1252, "Function declarations are not allowed inside blocks in strict mode when "
                "targeting 'ES5'. Modules are automatically in strict mode.");
            break;
        default:
            error_ctx_error_d(my errs, name, 1250,
                "Function declarations are not allowed inside blocks in strict mode when targeting 'ES5'.");
    }
}

Scope* scope_new_self(Scope* parent, Symbol const* self, bool declared) {
    if (!declared && (!unused_enabled() || !self)) return parent;
    Scope* ret = scope_new(parent, SCOPE_BLOCK);
    ret->self = self;
    ret->ambient |= declared;
    return ret;
}

Scope* scope_new_file(Binder* binder, SourceFile* file) {
    Scope* ret = scope_new(binder->global_scope, file->is_module ? SCOPE_MODULE : SCOPE_SCRIPT);
    ret->file = file;
    ret->ambient = file->is_declaration;
    // (tsc's bindInStrictMode)
    ret->strict = (compiler_options()->always_strict && !file->is_declaration) || file->is_module ||
        block_has_use_strict(&file->ast);
    if (file->is_module) {
        ret->container = binder_new_module_symbol(binder, file);
        ret->export_context = ret->ambient && !block_has_export_declarations(&file->ast);
    }
    return ret;
}

static OuterGenerics* push_generics(Scope* me, Generic const* generic, Span owner) {
    OuterGenerics* outer = arena_new(&my binder->arena, OuterGenerics);
    *outer = (OuterGenerics){ .parent = my generics, .generic = generic, .owner = owner, .owner_class = span_null };
    my generics = outer;
    return outer;
}

Scope* scope_new_generic(Scope* parent, Generic* generic, Span owner, Span owner_class, ErrorCtx* errs) {
    if (generic_empty(generic)) return parent;
    Scope* ret = scope_new(parent, SCOPE_BLOCK);
    push_generics(ret, generic, owner)->owner_class = owner_class;
    generic_add_names(generic, ret);
    generic_lookup_names(generic, ret, errs);
    return ret;
}

// The type parameters of an earlier declaration of `owner` (a class or interface)
static Generic const* earlier_generic(Symbol const* owner, void const* node, u32* index) {
    for (u32 i = 0; i < owner->decl_count; i++) {
        SymbolDecl const* decl = &owner->decls[i];
        if (decl->node == node) return nullptr;
        if (decl->kind == SYMBOL_DECL_INTERFACE || decl->kind == SYMBOL_DECL_CLASS) {
            *index = i;
            return decl->kind == SYMBOL_DECL_INTERFACE ? &((InterfaceDecl const*)decl->node)->generic :
                &((ClassDecl const*)decl->node)->generic;
        }
    }
    return nullptr;
}

Scope* scope_new_generic_merged(
    Scope* parent, Generic* generic, Symbol const* owner, void const* node, Span owner_name, ErrorCtx* errs
) {
    if (generic_empty(generic)) return parent;
    Scope* ret = scope_new(parent, SCOPE_BLOCK);
    push_generics(ret, generic, owner_name);
    // Like tsc, which declares them in the members of the merged symbol: a type parameter of the same name as one of
    // an earlier declaration is that one
    u32 index;
    Generic const* first = owner ? earlier_generic(owner, node, &index) : nullptr;
    for_each (i, vec_generic_item, &generic->items) {
        Symbol* shared = nullptr;
        Symbol const* shared_lib = nullptr;
        for_each (j, vec_generic_item, first ? &first->items : &(VecGenericItem){0}) {
            if (!j->owned_symbol || !csview_equals_sv(j->name.sv, i->name.sv)) continue;
            // (Not one of the shared lib program: nothing is written to its symbols)
            if (j->owned_symbol->binder == ret->binder) shared = j->owned_symbol;
            else shared_lib = j->owned_symbol;
        }
        if (!shared || symbol_table_get(&ret->locals, i->name.sv)) {
            if (shared_lib && !symbol_table_get(&ret->locals, i->name.sv)) {
                // Merged with a lib declaration (`interface Array<T>`): a symbol of our own that is declared by the
                // lib's type parameter first, like tsc's merged symbol
                Symbol* symbol = binder_new_symbol(ret->binder, i->name.sv, SYMBOL_TYPE_PARAMETER);
                symbol->name_span = shared_lib->name_span;
                for (u32 k = 0; k < shared_lib->decl_count; k++) symbol_add_decl(symbol, shared_lib->decls[k]);
                symbol_table_insert(&ret->locals, &ret->binder->arena, i->name.sv, symbol_name_hash(i->name.sv), symbol);
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
            symbol_table_insert(&ret->locals, &ret->binder->arena, i->name.sv, symbol_name_hash(i->name.sv), shared);
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

static Error* report_conflict(ErrorCtx* errs, Span at, unsigned code) {
    if (!span_valid(at) || !at.source || at.source->is_stdlib) return nullptr;
    // Once per declaration: a later conflict adds to the error that's there (tsc's lookupOrIssueError)
    Error* existing = error_ctx_find(errs, at, code == 2567 || code == 2451 ? code : 2300);
    if (existing) return existing;
    // (A missing name is tsc's "__missing", shown as "(Missing)")
    csview name = at.sv.size ? at.sv : c_sv("(Missing)");
    switch (code) {
        case 2567:
            return error_ctx_error_d(errs, at, 2567,
                "Enum declarations can only merge with namespace or other enum declarations.");
        case 2451:
            return error_ctx_error_d(errs, at, 2451, "Cannot redeclare block-scoped variable '", name, "'.");
        default:
            return error_ctx_error_d(errs, at, 2300, "Duplicate identifier '", name, "'.");
    }
}

// tsc's addDuplicateDeclarationError: the checker's merges (across files) point each error at the other side's
// declarations, the binder's (within a file) don't
void add_duplicate_related(Error* error, Span at, Span other) {
    if (!error || span_eq(at, other) || !span_valid(other) || !other.source || other.source->is_stdlib) return;
    u32 count = 0;
    for_each (i, vec_error, &error->sub_errors) {
        if (!span_valid(i->span)) continue;
        if (span_eq(i->span, other)) return;
        count++;
    }
    if (count >= 5) return;
    if (count) {
        error_add_related_d(error, other, 6204, "and here.");
    } else {
        error_add_related_d(error, other, 6203, "'", at, "' was also declared here.");
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
        if (!span_valid(at) || !at.source || at.source->is_stdlib) continue;
        Error* error = error_ctx_error_d(errs, at, 2528, "A module cannot have multiple default exports.");
        if (!span_valid(name)) continue;
        if (i == 0) error_add_related_d(error, name, 2753, "Another export default is here.");
        else error_add_related_d(error, name, 6204, "and here.");
    }
    if (!span_valid(name) || !name.source || name.source->is_stdlib) return;
    Error* error = error_ctx_error_d(errs, name, 2528, "A module cannot have multiple default exports.");
    for (u32 i = 0; i < existing->decl_count; i++) {
        Span at = default_error_span(&existing->decls[i]);
        if (span_valid(at)) error_add_related_d(error, at, 2752, "The first export default is here.");
    }
}

static void report_conflicts(ErrorCtx* errs, Symbol const* existing, Span name, u32 includes, bool related) {
    if (existing->is_global_this) {
        // tsc's mergeSymbol is silent about the built-in `globalThis`: what a script declares by that name is
        // initializeTypeChecker's error (a module's `declare global` declarations aren't)
        if (span_valid(name) && name.source && !name.source->is_stdlib && !name.source->is_module) {
            error_ctx_error_d(errs, name, 2397, "Declaration name conflicts with built-in global identifier 'globalThis'.");
        }
        return;
    }
    bool same_file = existing->decl_count && existing->decls[0].name.source == name.source;
    if (!same_file && existing->flags & SYMBOL_NAMESPACE_MODULE && !existing->is_global_this) {
        // Across files symbols are merged by the checker in tsc, whose mergeSymbol says this into a namespace
        if (span_valid(name) && name.source && !name.source->is_stdlib) {
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
    } else if (!same_file && includes & SYMBOL_BLOCK_SCOPED_VARIABLE) {
        // Across files symbols are merged by the checker in tsc, which looks at both sides
        code = 2451;
    }
    related |= !same_file;
    // (tsc's mergeSymbol: a plain JavaScript file's side of a conflict across files isn't reported, the binder's
    // conflicts inside one file are)
    for (u32 i = 0; i < existing->decl_count; i++) {
        Span at = existing->decls[i].name;
        if (!same_file && at.source && at.source->plain_js) continue;
        Error* error = report_conflict(errs, at, code);
        if (related) add_duplicate_related(error, at, name);
    }
    if (!same_file && name.source && name.source->plain_js) return;
    Error* error = report_conflict(errs, name, code);
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
    if (original->decl_count) {
        ret->decls = arena_new_n(&binder->arena, SymbolDecl, original->decl_cap);
        memcpy(ret->decls, original->decls, sizeof(SymbolDecl) * original->decl_count);
    }
    if (original->exports) {
        ret->exports = arena_new(&binder->arena, SymbolTable);
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
static bool symbol_is_builtin_type(Symbol const* symbol) {
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
    if (!span_valid(name) || !name.source || name.source->is_stdlib || !type_name_is_reserved(name.sv)) return;
    error_ctx_error_d(errs, name, code, what, " name cannot be '", name, "'.");
}

// tsc's checkCollisionWithRequireExportsInGeneratedCode: a declaration at the top level of a module that is emitted as
// CommonJS, AMD, UMD or System can't be named `require` or `exports`
static void check_require_exports_collision(Scope* me, Span name) {
    if (my kind != SCOPE_MODULE || my ambient || !my file || my file->unchecked || !span_valid(name)) return;
    if (!csview_equals(name.sv, "require") && !csview_equals(name.sv, "exports")) return;
    // (tsc's errorSkippedOn("noEmit"))
    if (compiler_options()->no_emit || source_file_emit_module_kind(my file) >= MODULE_KIND_ES2015) return;
    error_ctx_error_d(
        my errs, name, 2441, "Duplicate identifier '", name, "'. Compiler reserves name '", name,
        "' in top level scope of a module."
    );
}

// tsc's declareSymbol(). *merged is whether the symbol existed already. default_export: a declaration of the `default`
// export of a module, which conflicts with the others as TS2528
// Whether a top-level assignment of a JavaScript file makes `name` a namespace: `name.a = v`, `name.a.b = v`,
// `name.prototype = v` (tsc's bindPotentiallyMissingNamespaces gives it SymbolFlags.Assignment), not `name.prototype.a = v`
static bool js_file_assigns_namespace(SourceFile const* file, csview name) {
    if (!file || !file->is_js) return false;
    for_each (i, vec_statement, &file->ast.statements) {
        if (i->tag != STATEMENT_EXPR) continue;
        for (Expr const* e = &i->expr; e->tag == EXPR_BINARY_OP && e->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(e);
            e = &e->binary_op->right
        ) {
            Expr const* left = &e->binary_op->left;
            if (!span_valid(expr_static_property_name(left))) continue;
            Expr const* object = &left->property->expr;
            if (expr_prototype_object((Expr*)object)) continue;
            Expr const* root = object;
            while (span_valid(expr_static_property_name(root))) root = &root->property->expr;
            if (root->tag == EXPR_IDENTIFIER && !root->identifier.keyword && !expr_is_perenthisized(root) &&
                csview_equals_sv(root->identifier.name.sv, name) && expr_is_entity_name(object)
            ) {
                return true;
            }
        }
    }
    return false;
}

// A global declared in other files that a JavaScript file's top-level assignment makes a namespace, or the other way
// around, merges with anything (tsc's mergeSymbol: the Assignment flag of either side). Conflicts inside a file stay
static bool global_merges_by_assignment(Scope const* scope, Symbol const* existing, csview name) {
    SourceFile const* file = scope->file;
    bool assigned = js_file_assigns_namespace(file, name);
    for (u32 i = 0; i < existing->decl_count; i++) {
        SourceFile const* other = existing->decls[i].full.source;
        if (!other || other == file) return false;
        if (!assigned) assigned = js_file_assigns_namespace(other, name);
    }
    return assigned;
}

static Symbol* declare_symbol(
    Scope* scope, SymbolTable* table, csview name, Span name_span, u32 includes, u32 excludes, bool default_export,
    bool* merged
) {
    Binder* binder = scope->binder;
    u64 hash = symbol_name_hash(name);
    *merged = false;
    Symbol* existing = symbol_table_get_own(table, name, hash);
    // `class number {}` (TS2414): the keyword wins, the declaration is a symbol of its own
    Symbol const* builtin = existing ? existing : table->parent ? symbol_table_lookup(table->parent, name, hash) : nullptr;
    if (symbol_is_builtin_type(builtin) && builtin->flags & excludes) return binder_new_symbol(binder, name, includes);
    if (!existing && table->parent) {
        Symbol* inherited = symbol_table_lookup(table->parent, name, hash);
        if (inherited) {
            if (inherited->flags & excludes) {
                report_conflicts(scope->errs, inherited, name_span, includes, false);
                return binder_new_symbol(binder, name, includes);
            }
            existing = clone_parent_symbol(binder, inherited);
            symbol_table_insert(table, &binder->arena, existing->name, hash, existing);
        }
    }
    if (existing) {
        // A global namespace JavaScript expandos of another file made merges with anything (tsc's mergeSymbol: the
        // Assignment flag)
        bool expando_only = existing->decl_count > 0;
        for (u32 i = 0; i < existing->decl_count && expando_only; i++) expando_only = existing->decls[i].kind == SYMBOL_DECL_EXPANDO;
        if (existing->flags & excludes && !(expando_only && table == &binder->globals) &&
            !(table == &binder->globals && global_merges_by_assignment(scope, existing, name))
        ) {
            if (default_export && existing->decl_count) report_multiple_defaults(scope->errs, existing, name_span);
            else report_conflicts(scope->errs, existing, name_span, includes, false);
            return binder_new_symbol(binder, name, includes);
        }
        existing->decl_flags_before = existing->flags;
        existing->flags |= includes;
        if (includes & SYMBOL_ALIAS && !existing->alias) existing->alias = arena_new(&binder->arena, SymbolAlias);
        *merged = true;
        return existing;
    }
    Symbol* ret = binder_new_symbol(binder, name, includes);
    symbol_table_insert(table, &binder->arena, name, hash, ret);
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
    return &my locals;
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
        case SYMBOL_DECL_EXPORT_EQUALS_PROPERTY:
        case SYMBOL_DECL_NONE:
            break;
    }
    panic("declaration kind ", (i32)props->kind, " can't be declared in a scope");
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
        scope_check_strict_identifier(me, props.name, false);
    }
    // (Parameters and function names only outside ambient contexts, like tsc's bindParameter)
    if ((props.kind == SYMBOL_DECL_VARIABLE && !(props.is_parameter && me->ambient)) ||
        (props.kind == SYMBOL_DECL_FUNCTION && !me->ambient)
    ) {
        scope_check_strict_eval_or_arguments(me, props.name);
    }

    check_type_name_is_reserved(me->errs, props.name, props.kind);
    if (!props.is_ambient && (
        (props.kind == SYMBOL_DECL_VARIABLE && !props.is_parameter) || props.kind == SYMBOL_DECL_FUNCTION ||
        props.kind == SYMBOL_DECL_CLASS || props.kind == SYMBOL_DECL_ENUM ||
        (props.kind == SYMBOL_DECL_NAMESPACE && props.is_instantiated)
    )) {
        check_require_exports_collision(me, props.name);
    }
    u32 includes, excludes;
    symbol_flags_of_props(&props, &includes, &excludes);
    if (props.default_export && !(scope->container && scope->container->is_module)) {
        props.default_export = false; // TODO: TS1319 and friends
    }
    SymbolTable* table;
    csview name = props.name.sv;
    if (props.default_export) {
        symbol_ensure_exports(scope->container);
        table = scope->container->exports;
        name = c_sv("default");
    } else {
        table = scope_declaration_table(scope, props.exported);
    }
    bool merged;
    Span error_at = props.default_export && !span_valid(props.name) ? first_token(props.full) : props.name;
    Symbol* symbol = declare_symbol(scope, table, name, error_at, includes, excludes, props.default_export, &merged);
    u32 flags_before = merged ? symbol->decl_flags_before : 0;
    if (merged && includes & flags_before & SYMBOL_TYPE_PARAMETER && props.kind != SYMBOL_DECL_INFER) {
        // Type parameters of the same name merge as far as tsc's binder is concerned, which is what `infer U` twice
        // in one conditional type wants. <T, T>: one symbol declared twice (and one type), the second is TS2300
        report_conflict(scope->errs, props.name, 2300);
    }
    if (props.kind == SYMBOL_DECL_VARIABLE) {
        // (Also when it merges into something that isn't a variable: a `declare const never` and the type `never`)
        symbol->is_const = props.is_const;
        symbol->is_parameter = props.is_parameter;
    }
    // (A function too: an ambient `require` function is CommonJS's, expr_symbol_is_commonjs_require())
    if (props.kind == SYMBOL_DECL_VARIABLE || props.kind == SYMBOL_DECL_CLASS || props.kind == SYMBOL_DECL_ENUM ||
        props.kind == SYMBOL_DECL_FUNCTION
    ) {
        symbol->is_ambient |= scope->ambient || props.is_ambient;
    }
    if (!merged && props.default_export) {
        // Named like its (first) declaration (tsc's getNameOfSymbolAsWritten), and exported like the declarations
        // `export default x` refers to
        symbol->name = span_valid(props.name) ? props.name.sv : c_sv("default");
        symbol->name_span = props.name;
        symbol->is_default_export = true;
        symbol->referenced = SYMBOL_ANY_MEANING;
        symbol->parent = scope->container;
    } else if (props.default_export && !span_valid(symbol->name_span) && span_valid(props.name)) {
        // Named by its first named declaration
        symbol->name = props.name.sv;
        symbol->name_span = props.name;
    } else if (!merged) {
        // Like in tsc only exports have a parent: a local of a namespace is just `x`, not `N.x`
        if (scope->container && table == scope->container->exports) symbol->parent = scope->container;
        symbol->name_span = props.name;
        symbol->exported = props.exported;
        if (table == &scope->locals) {
            symbol->is_local = true;
            symbol->function = scope->function;
        }
    }
    symbol_add_decl(symbol, (SymbolDecl){
        .kind = props.kind, .flags = includes, .node = props.node, .name = props.name, .full = props.full, .block = scope,
    });
    if (props.default_export && span_valid(props.name)) scope_declare_default_local(scope, symbol, props.name, excludes);
    if (includes & (SYMBOL_NAMESPACE_MODULE | SYMBOL_VALUE_MODULE | SYMBOL_ENUM)) symbol_ensure_exports(symbol);
    *ret = symbol;
}

// `export default class A`: a local `A` without meanings of its own, which conflicts with other local declarations of
// `A` like tsc's (the local symbol of declareModuleMember, only a value's has ExportValue). Looking `A` up finds the
// default export, see symbol_default_export_local_name()
static void scope_declare_default_local(Scope* me, Symbol* symbol, Span name, u32 excludes) {
    bool merged;
    Symbol* local = declare_symbol(me, &my locals, name.sv, name, SYMBOL_NONE, excludes, false, &merged);
    if (!merged) {
        local->name_span = name;
        local->is_local = true;
        local->referenced = SYMBOL_ANY_MEANING;
    }
    if (!local->export_symbol) local->export_symbol = symbol;
    SymbolDecl decl = symbol->decls[symbol->decl_count - 1];
    decl.flags = SYMBOL_NONE;
    symbol_add_decl(local, decl);
}

// tsc's getDeclarationSpaces (without aliases, which would need resolving)
enum { SPACE_TYPE = 1, SPACE_VALUE = 2, SPACE_NAMESPACE = 4 };
static u32 declaration_spaces(SymbolDecl const* decl) {
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
        default:
            return 0;
    }
}

static bool span_within(Span inner, Span outer) {
    return inner.source == outer.source && span_start_ptr(inner) >= span_start_ptr(outer) &&
        span_end_ptr(inner) <= span_end_ptr(outer);
}

void scope_check_export_merges(Scope* me, Span block) {
    if (!my container || !my container->exports || !span_valid(block)) return;
    for (u32 i = 0; i < my locals.count; i++) {
        Symbol const* local = my locals.entries[i].symbol;
        Symbol const* exported = symbol_table_get(my container->exports, local->name);
        if (exported == local) exported = nullptr;
        if (!exported && !local->export_symbol) continue;
        // The exported declarations of this block merge with its local ones (tsc: one local symbol), and so do those
        // of `export default class A` (the local's declarations without flags of their own)
        u32 exported_spaces = 0, local_spaces = 0, default_spaces = 0;
        for (u32 d = 0; exported && d < exported->decl_count; d++) {
            if (span_within(exported->decls[d].full, block)) exported_spaces |= declaration_spaces(&exported->decls[d]);
        }
        for (u32 d = 0; d < local->decl_count; d++) {
            if (local->decls[d].flags) local_spaces |= declaration_spaces(&local->decls[d]);
            else default_spaces |= declaration_spaces(&local->decls[d]);
        }
        u32 common = exported_spaces & local_spaces;
        u32 common_default = default_spaces & (exported_spaces | local_spaces);
        if (!common && !common_default) continue;
        Symbol const* symbols[] = { local, exported };
        for (u32 s = 0; s < 2; s++) {
            for (u32 d = 0; symbols[s] && d < symbols[s]->decl_count; d++) {
                SymbolDecl const* decl = &symbols[s]->decls[d];
                if (s == 1 && !span_within(decl->full, block)) continue;
                if (!span_valid(decl->name)) continue;
                u32 spaces = declaration_spaces(decl);
                if (spaces & common_default) {
                    error_ctx_error_d(my errs, decl->name, 2652, "Merged declaration '", decl->name,
                        "' cannot include a default export declaration. Consider adding a separate 'export default ",
                        decl->name, "' declaration instead.");
                } else if (spaces & common && (s == 1 || decl->flags)) {
                    error_ctx_error_d(my errs, decl->name, 2395, "Individual declarations in merged declaration '",
                        decl->name, "' must be all exported or all local.");
                }
            }
        }
    }
}

// `export default function () {}`: its first token (tsc's getErrorSpanForNode without a name)
static Span first_token(Span full) {
    if (!span_valid(full)) return full;
    isize len = 0;
    while (len < full.sv.size && (isalnum((unsigned char)full.sv.buf[len]) || full.sv.buf[len] == '_')) len++;
    full.sv.size = len ? len : 1;
    return full;
}

Span symbol_decl_error_span(SymbolDecl const* decl) {
    // `export default x` (tsc: the ExportAssignment)
    if (decl->kind == SYMBOL_DECL_EXPR || (decl->kind == SYMBOL_DECL_ALIAS && !decl->node)) return decl->full;
    return span_valid(decl->name) ? decl->name : first_token(decl->full);
}

static void check_module_exports(Symbol const* container, ErrorCtx* errs) {
    if (!container || !container->exports) return;
    SymbolTable const* exports = container->exports;
    Symbol const* export_equals = symbol_table_get(exports, c_sv("export="));
    if (export_equals && exports->count > 1 && export_equals->decl_count) {
        // (tsc's checkExternalModuleExports)
        error_ctx_error_d(errs, export_equals->decls[0].full, 2309,
            "An export assignment cannot be used in a module with other exported elements.");
    }
    for (u32 i = 0; i < exports->count; i++) {
        Symbol const* symbol = exports->entries[i].symbol;
        if (symbol->flags & SYMBOL_NAMESPACE) continue; // (Namespaces and enums merge)
        u32 count = 0;
        for (u32 d = 0; d < symbol->decl_count; d++) {
            SymbolDecl const* decl = &symbol->decls[d];
            if (decl->kind == SYMBOL_DECL_INTERFACE) continue;
            if (decl->kind == SYMBOL_DECL_FUNCTION && !((FunctionImpl const*)decl->node)->has_body) continue;
            count++;
        }
        // A type alias may merge with one value
        if (count <= 1 || ((symbol->flags & SYMBOL_TYPE_ALIAS) && count <= 2)) continue;
        for (u32 d = 0; d < symbol->decl_count; d++) {
            SymbolDecl const* decl = &symbol->decls[d];
            if (decl->kind == SYMBOL_DECL_FUNCTION && !((FunctionImpl const*)decl->node)->has_body) continue;
            error_ctx_error_d(errs, symbol_decl_error_span(decl), 2323, "Cannot redeclare exported variable '", exports->entries[i].name,
                "'.");
        }
    }
}

void scope_check_duplicate_exports(Scope* me) {
    check_module_exports(my container, my errs);
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
            if (span_valid(item->modifiers)) error_ctx_error_d(my errs, item->modifiers, 1184, "Modifiers cannot appear here.");
            if (item->not_top_level) {
                error_ctx_error_d(my errs, item->full_span, 1316, "Global module exports may only appear at top level.");
                return;
            } else if (my kind != SCOPE_MODULE) {
                error_ctx_error_d(my errs, item->full_span, 1314, "Global module exports may only appear in module files.");
                return;
            } else if (my file && !my file->is_declaration) {
                error_ctx_error_d(my errs, item->full_span, 1315, "Global module exports may only appear in declaration files.");
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
                PortItem** stars = arena_new_n(&my binder->arena, PortItem*, cap);
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
        if (!item->type_only) check_require_exports_collision(me, item->decl_name);
    }
    bool exported = port_item_declares_export(item);
    if (port_item_is_export(item) && !(my container && my kind != SCOPE_ENUM)) {
        return; // TODO: TS1194 export declarations are not permitted in a namespace... and nowhere else either
    }
    // (An alias is exported only by `export`: an ambient export context doesn't export imports, tsc's
    // declareModuleMember)
    SymbolTable* table = !exported && (my kind == SCOPE_MODULE || my kind == SCOPE_NAMESPACE) ? &my locals :
        scope_declaration_table(me, exported);
    bool merged;
    csview name = port_item_name(item->decl_name);
    bool default_export = exported && my container && my container->is_module && csview_equals(name, "default");
    Symbol* symbol = declare_symbol(
        me, table, name, item->decl_name, SYMBOL_ALIAS, SYMBOL_ALIAS_EXCLUDES, default_export, &merged
    );
    if (!merged) {
        symbol->parent = my container;
        symbol->name_span = item->decl_name;
        symbol->exported = exported;
        symbol->is_local = table == &my locals;
    }
    symbol_add_decl(symbol, (SymbolDecl){
        .kind = SYMBOL_DECL_ALIAS, .flags = SYMBOL_ALIAS, .node = item, .name = item->decl_name, .full = item->full_span,
    });
    symbol->alias->item = item;
    symbol->alias->scope = me;
    item->symbol = symbol;

    bool needs_module = item->tag != PORT_IMPORT_ALIAS && item->tag != PORT_EXPORT_SPECIFIC;
    if (needs_module && !item->module_ref) {
        // The parser complained
        symbol->alias->target = binder_new_placeholder(my binder, item->decl_name, item->full_span);
        symbol->alias->state = ALIAS_DONE;
    }
}

void scope_export_default_expr(Scope* me, Expr* expr, TypeNode const* jsdoc_type, Span statement, bool export_equals) {
    // tsc's checkExportAssignment -> checkGrammarModuleElementContext: only in a file or a namespace body (on the
    // first token; nothing is exported)
    bool top = my kind == SCOPE_MODULE || my kind == SCOPE_SCRIPT || my kind == SCOPE_GLOBAL_AUGMENTATION ||
        my kind == SCOPE_NAMESPACE || (my container && my container->is_module && my kind != SCOPE_BLOCK &&
            my kind != SCOPE_FUNCTION && my kind != SCOPE_STATIC_BLOCK);
    if (!top && span_valid(statement) && csview_starts_with(statement.sv, "export")) {
        Span first = span_make(statement.source, statement.sv.buf, statement.sv.buf + 6);
        if (export_equals) {
            error_ctx_grammar_error_d(my errs, first, 1231, "An export assignment must be at the top level of a file or module declaration.");
        } else {
            error_ctx_grammar_error_d(my errs, first, 1258, "A default export must be at the top level of a file or module declaration.");
        }
        return;
    }
    if (!my container || !my container->is_module) return; // TODO: TS1319 and friends
    symbol_ensure_exports(my container);
    csview export_name = export_equals ? c_sv("export=") : c_sv("default");
    // Everything the name means is exported, types included; other expressions are a value. Nothing else can be
    // exported under the name (tsc's bindExportAssignment: SymbolFlags.All)
    bool alias = expr->tag == EXPR_IDENTIFIER && !expr->identifier.keyword;
    u32 includes = alias ? SYMBOL_ALIAS : SYMBOL_BLOCK_SCOPED_VARIABLE;
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
            error_ctx_grammar_error_d(my errs, statement, 1203,
                "Export assignment cannot be used when targeting ECMAScript modules. Consider using 'export default' or "
                "another module format instead.");
        }
    }
    // (Not an augmentation: tsc skips those)
    if (my kind == SCOPE_NAMESPACE && my file && !my file->is_module && my container->module) {
        my container->module->has_export_assignment = true;
    }
    Span name = alias ? expr->outer_span : statement;
    if (expr->tag == EXPR_IMPLICIT_ANY && span_valid(expr->outer_span) && !expr->outer_span.sv.size) {
        // A missing expression is a missing identifier to tsc, at its full start (the end of `default`)
        char const* at = span_start_ptr(expr->outer_span);
        char const* file_start = span_start_ptr(statement);
        while (at > file_start && (at[-1] == ' ' || at[-1] == '\t')) at--;
        name = span_make(expr->outer_span.source, at, at);
    }
    Symbol* symbol = declare_symbol(
        me, my container->exports, export_name, name, includes, SYMBOL_ANY_MEANING | SYMBOL_ALIAS, !export_equals, &merged
    );
    if (!merged) {
        symbol->name = alias ? expr->identifier.name.sv : export_name;
        symbol->name_span = alias ? expr->identifier.name : statement;
        symbol->parent = my container;
        symbol->is_default_export = !export_equals;
    }
    if (alias) {
        if (!symbol->alias) symbol->alias = arena_new(&my binder->arena, SymbolAlias);
        symbol->alias->scope = me;
        symbol->alias->name = expr->identifier.name;
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_ALIAS, .flags = SYMBOL_ALIAS, .name = expr->outer_span, .full = statement,
            .jsdoc_type = jsdoc_type,
        });
    } else {
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_EXPR, .flags = SYMBOL_BLOCK_SCOPED_VARIABLE, .node = expr,
            .name = statement, .full = statement, .jsdoc_type = jsdoc_type,
        });
    }
}

// CommonJS

// `exports.x = v`: an alias when v is a name (tsc's isAliasableExpression), else a property whose type is what is
// assigned (several assignments merge)
// A function expression assigned to a declared property (`exports.F = function () {}`, `ns.F = function () {}`) is a
// class of that symbol if it assigns `this.x` (tsc's bindThisPropertyAssignment: the function's own symbol)
static Expr const* defaulted_initializer(Expr const* value);

static void set_function_class_symbol(Expr* value, Symbol* symbol) {
    value = (Expr*)defaulted_initializer(value);
    if (value->tag != EXPR_FUNCTION || span_valid(value->function->arrow_span) || value->function->class_symbol) return;
    SourceFile const* file = value->inner_span.source;
    if (file && file->is_js) value->function->class_symbol = symbol;
    if (file && file->is_js && symbol) scope_mark_jsdoc_class(symbol->binder, symbol, value->function);
}

void scope_mark_jsdoc_class(Binder* binder, Symbol* symbol, FunctionImpl const* function) {
    if (!symbol || !function->jsdoc_class) return;
    symbol->flags |= SYMBOL_CLASS;
    if (!symbol->members) symbol->members = arena_new(&binder->arena, SymbolTable);
}

static void declare_commonjs_export(
    Scope* me, csview name, Span name_span, Expr* value, TypeNode const* jsdoc_type, Span left, Span statement,
    AssignmentDeclKind kind, bool top_level
) {
    symbol_ensure_exports(my container);
    // (A class expression is an alias of its class, tsc's isAliasableExpression: at the top level, where the declare
    // step can make the class's symbol in the scope the resolve walk would)
    bool class_alias = value->tag == EXPR_CLASS_DECL && !expr_is_perenthisized(value) && top_level &&
        (kind == ASSIGNMENT_DECL_PROPERTY || kind == ASSIGNMENT_DECL_EXPORTS_ALIAS_PROPERTY) && !jsdoc_type;
    bool alias = (value->tag == EXPR_IDENTIFIER && !value->identifier.keyword) || class_alias;
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
        if (!symbol->alias) symbol->alias = arena_new(&my binder->arena, SymbolAlias);
        symbol->alias->scope = me;
        if (class_alias) {
            ClassDecl* class_decl = value->class_decl;
            if (!class_decl->name_scope) {
                class_decl->name_scope = scope_new(me, SCOPE_BLOCK);
                class_decl_add_names(class_decl, class_decl->name_scope, DECL_EXPORTED_NO, false);
            }
            symbol->alias->class_expr = class_decl;
            class_decl->commonjs_module = my container;
        } else {
            symbol->alias->name = value->identifier.name;
        }
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_ALIAS, .flags = SYMBOL_ALIAS, .name = left, .full = statement,
        });
    } else {
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_ASSIGNMENT, .assignment = kind, .flags = SYMBOL_PROPERTY, .node = value, .name = left,
            .full = statement, .jsdoc_type = jsdoc_type,
        });
        set_function_class_symbol(value, symbol);
    }
}

// tsc's getRightMostAssignedExpression: `module.exports = exports = x` assigns x
static Expr* rightmost_assigned(Expr* value) {
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    return value;
}

static void add_commonjs_export(Scope* me, Expr* assignment, bool top_level, bool in_function);

// tsc's isExportsOrModuleExportsOrAlias: `exports`, `module.exports`, or a variable of the module initialized with one
// of them (through assignments: `var a = module.exports = {}`)
static bool is_exports_alias(Scope* module, Expr const* e) {
    Expr const* queue[16];
    u32 head = 0, tail = 0;
    queue[tail++] = e;
    for (u32 steps = 0; head < tail && steps < 100; steps++) {
        Expr const* node = queue[head++];
        if (expr_is_perenthisized(node)) continue;
        if (node->tag == EXPR_IDENTIFIER && !node->identifier.keyword && span_equals(node->identifier.name, "exports")) return true;
        if (expr_is_module_exports(node)) return true;
        if (node->tag != EXPR_IDENTIFIER || node->identifier.keyword) continue;
        Symbol const* symbol = symbol_table_get(&module->locals, node->identifier.name.sv);
        Binding const* binding = symbol ? symbol_find_node(symbol, SYMBOL_DECL_VARIABLE) : nullptr;
        if (!binding || binding->parent || binding->tag != BINDING_IDENTIFIER) continue;
        Expr const* init = &((InitializedBinding const*)binding)->value;
        if (init->tag == EXPR_IMPLICIT_ANY || tail + 3 > c_arraylen(queue)) continue;
        queue[tail++] = init;
        if (init->tag == EXPR_BINARY_OP && init->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(init)) {
            queue[tail++] = &init->binary_op->left;
            queue[tail++] = &init->binary_op->right;
        }
    }
    return false;
}

// The module scope of a file's top level, through the blocks at its top level (tsc's container, which blocks aren't)
static Scope* commonjs_module_scope(Scope* me) {
    while (me && me->kind == SCOPE_BLOCK) me = me->parent;
    return me && me->kind == SCOPE_MODULE && me->file && me->file->is_commonjs && me->container ? me : nullptr;
}

// `Object.defineProperty(exports, "x", {...})` (tsc's bindObjectDefinePropertyExport): an export declared by the call
static void add_define_property_export(Scope* me, Expr* call, bool top_level) {
    Span name;
    if (!expr_is_define_property_call(call, &name) || !expr_is_exports_object(vec_expr_at(&call->call->args, 0))) return;
    declare_commonjs_export(me, name.sv, name, call, nullptr, call->outer_span, call->outer_span, ASSIGNMENT_DECL_DEFINE_PROPERTY, top_level);
    call->call->declares = symbol_table_get(my container->exports, name.sv);
}

void scope_add_commonjs_export(Scope* me, Expr* assignment) {
    if (my kind != SCOPE_MODULE || !my file || !my file->is_commonjs || !my container) return;
    // Every assignment of a chain (`exports = module.exports = C`)
    while (assignment->tag == EXPR_BINARY_OP && assignment->binary_op->op == BINARY_OP_EQUAL) {
        add_commonjs_export(me, assignment, true, false);
        assignment = &assignment->binary_op->right;
    }
    add_define_property_export(me, assignment, true);
}

// Declared by the assignment expression (tsc's declaration node, `exports = module.exports = C` has two)
static void add_commonjs_export(Scope* me, Expr* assignment, bool top_level, bool in_function) {
    BinaryOpExpr* op = assignment->binary_op;
    Span statement = assignment->outer_span;
    Span name = span_null;
    switch (binary_commonjs_declaration(op, &name)) {
        case COMMONJS_TARGET_NONE: {
            // `a.x = v` where a is `module.exports` by another name
            Expr* left = &op->left;
            name = left->tag == EXPR_PROPERTY ? expr_static_property_name(left) : span_null;
            if (op->op != BINARY_OP_EQUAL || !span_valid(name) || name.sv.buf[0] == '#') break;
            Expr const* object = &left->property->expr;
            if (object->tag != EXPR_IDENTIFIER || object->identifier.keyword || !is_exports_alias(me, object)) break;
            if (span_equals(object->identifier.name, "exports")) break; // (binary_commonjs_declaration()'s)
            left->property->commonjs_export_alias = true;
            declare_commonjs_export(me, name.sv, name, rightmost_assigned(&op->right), op->jsdoc_type, left->outer_span, statement, ASSIGNMENT_DECL_EXPORTS_ALIAS_PROPERTY, top_level);
        }   break;
        case COMMONJS_TARGET_EXPORTS_PROPERTY: {
            // (`exports["b"] = v` is named `"b"` where it's shown, like tsc's symbol of a string literal name)
            Span shown = op->left.tag == EXPR_PROPERTY && op->left.property->form == PROPERTY_EXPR_BRACKET &&
                op->left.property->prop.tag == EXPR_LITERAL ? op->left.property->prop.inner_span : name;
            declare_commonjs_export(me, name.sv, shown, &op->right, op->jsdoc_type, op->left.outer_span, statement, ASSIGNMENT_DECL_PROPERTY, top_level);
        }   break;
        case COMMONJS_TARGET_MODULE_EXPORTS: {
            Expr* value = rightmost_assigned(&op->right);
            // `module.exports = exports` declares nothing (tsc's bindModuleExportsAssignment), at the top level: in a
            // function it's an alias of the module
            if (!in_function && is_exports_alias(me, value)) break;
            if (value->tag == EXPR_OBJECT) {
                // `module.exports = {}` declares nothing, `{ a, b }` its shorthands (tsc's
                // bindExportAssignedObjectMemberAlias)
                bool shorthands = true;
                for_each (i, vec_object_expr_item, &value->object) shorthands &= i->tag == OBJECT_ITEM_SHORTHAND;
                if (shorthands) {
                    for_each (i, vec_object_expr_item, &value->object) {
                        declare_commonjs_export(me, i->name_span.sv, i->name_span, &i->value, i->jsdoc_type, i->name_span, i->span, ASSIGNMENT_DECL_PROPERTY, top_level);
                    }
                    break;
                }
            }
            declare_commonjs_export(me, c_sv("export="), op->left.outer_span, value, op->jsdoc_type, op->left.outer_span, statement, ASSIGNMENT_DECL_PROPERTY, top_level);
        }   break;
    }
}

// The CommonJS exports of the statements nested in the top level's (`if (c) { exports.x = 1 }`): the file is their
// container (tsc's bindSpecialPropertyAssignment: container === file), declared with the others before linking
static void add_nested_commonjs_exports(Scope* module, Statement* statement, bool nested, bool in_function);

static void add_nested_commonjs_block(Scope* module, VecStatement* statements, bool in_function) {
    for_each (i, vec_statement, statements) add_nested_commonjs_exports(module, i, true, in_function);
}

static void add_nested_commonjs_exports(Scope* module, Statement* statement, bool nested, bool in_function) {
    switch (statement->tag) {
        case STATEMENT_EXPR:
            // An immediately invoked function's body too (tsc binds them from any container)
            if (statement->expr.tag == EXPR_CALL && !statement->expr.call->uses_new &&
                statement->expr.call->callable.tag == EXPR_FUNCTION
            ) {
                add_nested_commonjs_block(module, &statement->expr.call->callable.function->body.statements, true);
            }
            if (nested) {
                Expr* assignment = &statement->expr;
                while (assignment->tag == EXPR_BINARY_OP && assignment->binary_op->op == BINARY_OP_EQUAL) {
                    add_commonjs_export(module, assignment, false, in_function);
                    assignment = &assignment->binary_op->right;
                }
                add_define_property_export(module, assignment, false);
            }
            break;
        case STATEMENT_DECL_VARIABLE:
            // `var npm = module.exports = f`
            for_each (i, vec_initialized_binding, &statement->decl.variable.initializers) {
                Expr* assignment = &i->value;
                Symbol* export_equals = module->container->exports ? symbol_table_get(module->container->exports, c_sv("export=")) : nullptr;
                while (assignment->tag == EXPR_BINARY_OP && assignment->binary_op->op == BINARY_OP_EQUAL &&
                    !expr_is_perenthisized(assignment)
                ) {
                    add_commonjs_export(module, assignment, false, in_function);
                    assignment = &assignment->binary_op->right;
                }
                // `var m = module.exports = v`: the `export=` it declares first is named m (tsc's
                // getNameOfSymbolAsWritten: a declaration whose parent is a variable declaration)
                Symbol* declared = module->container->exports ? symbol_table_get(module->container->exports, c_sv("export=")) : nullptr;
                if (!export_equals && declared && i->binding.tag == BINDING_IDENTIFIER && i->value.tag == EXPR_BINARY_OP &&
                    expr_is_module_exports(&i->value.binary_op->left)
                ) {
                    declared->name_span = i->binding.content_span;
                }
            }
            break;
        case STATEMENT_BLOCK: add_nested_commonjs_block(module, &statement->block.statements, in_function); break;
        case STATEMENT_IF_ELSE: add_nested_commonjs_block(module, &statement->if_else.statements, in_function); break;
        case STATEMENT_FOR_LOOP: add_nested_commonjs_exports(module, &statement->for_loop->body, true, in_function); break;
        case STATEMENT_FOR_EACH_LOOP: add_nested_commonjs_exports(module, &statement->for_each_loop->body, true, in_function); break;
        case STATEMENT_LABELED: add_nested_commonjs_exports(module, statement->labeled.body, nested, in_function); break;
        case STATEMENT_SWITCH:
            for_each (i, vec_switch_case, &statement->switch_->cases) add_nested_commonjs_block(module, &i->statements, in_function);
            break;
        case STATEMENT_TRY_CATCH:
            add_nested_commonjs_block(module, &statement->try_catch->try_block.statements, in_function);
            if (statement->try_catch->has_catch) add_nested_commonjs_block(module, &statement->try_catch->catch_block.statements, in_function);
            if (statement->try_catch->has_finally) add_nested_commonjs_block(module, &statement->try_catch->finally_block.statements, in_function);
            break;
        default: break;
    }
}

void scope_add_commonjs_module(Scope* me) {
    if (my kind == SCOPE_MODULE && my file && my file->is_commonjs && my container && my block) {
        for_each (i, vec_statement, &((Block*)my block)->statements) add_nested_commonjs_exports(me, i, false, false);
    }
    SourceFile* file = my file;
    if (my kind != SCOPE_MODULE || !file || !file->is_commonjs || !span_valid(file->module_exports_span)) return;
    if (symbol_table_get(&my locals, c_sv("module"))) return;
    bool merged;
    Span name = file->module_exports_span;
    Symbol* symbol = declare_symbol(me, &my locals, name.sv, name, SYMBOL_FUNCTION_SCOPED_VARIABLE, 0, false, &merged);
    symbol->name = name.sv;
    symbol->name_span = name;
    symbol->is_local = true;
    symbol_add_decl(symbol, (SymbolDecl){
        .kind = SYMBOL_DECL_COMMONJS_MODULE, .flags = SYMBOL_FUNCTION_SCOPED_VARIABLE, .node = file, .name = name,
        .full = name,
    });
}

Symbol* binder_require_symbol(Binder* me) {
    if (!my require_symbol) {
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

static Symbol* binder_find_module(Binder const* me, ModuleRef const* ref) {
    csview path = cstr_sv(&ref->path);
    bool relative = csview_starts_with(path, "./") || csview_starts_with(path, "../") ||
        csview_equals(path, ".") || csview_equals(path, "..");
    if (!relative) {
        Symbol* ambient = symbol_table_get(&my ambient_modules, path);
        if (ambient) return ambient;
    }
    if (ref->file) return source_file_module_symbol(ref->file);
    // The longest prefix wins
    Symbol* best = nullptr;
    isize best_prefix = -1;
    for (Binder const* binder = me; binder; binder = binder->parent) {
        for (u32 i = 0; i < binder->pattern_module_count; i++) {
            isize prefix;
            if (pattern_module_matches(binder->pattern_modules[i], path, &prefix) && prefix > best_prefix) {
                best = binder->pattern_modules[i];
                best_prefix = prefix;
            }
        }
    }
    return best;
}

Symbol* binder_module_of_ref(Binder const* me, ModuleRef* ref) {
    if (!ref) return nullptr;
    if (ref->module_state != ALIAS_DONE) {
        ref->module = binder_find_module(me, ref);
        ref->module_state = ALIAS_DONE;
    }
    return ref->module;
}

// tsc's getCommonJsExportEquals: a module that has other exports next to its `export =` (a CommonJS
// `module.exports = C; module.exports.x = 1`, its `@typedef`s) exports a clone of what `export =` assigns, with the
// module's exports added to the clone's (what the assigned symbol has itself wins)
static Symbol* merged_export_equals(Symbol* module, Symbol* target) {
    if (module->module->merged_export_equals) return module->module->merged_export_equals;
    Binder* binder = module->binder;
    Symbol* ret = binder_new_symbol(binder, target->name, target->flags);
    *ret = *target;
    ret->binder = binder;
    ret->merged_export_equals_of = target;
    ret->flags |= SYMBOL_VALUE_MODULE; // (Something to look names up in)
    ret->exports = arena_new(&binder->arena, SymbolTable);
    ret->exports->parent = target->exports;
    for (u32 i = 0; i < module->exports->count; i++) {
        SymbolEntry const* entry = &module->exports->entries[i];
        if (csview_equals(entry->name, "export=") || symbol_table_get(target->exports, entry->name)) continue;
        symbol_table_insert(ret->exports, &binder->arena, entry->name, entry->hash, entry->symbol);
    }
    module->module->merged_export_equals = ret;
    return ret;
}

Symbol* module_export_equals(Symbol* module) {
    if (!module || !module->is_module) return module;
    Symbol* assigned = symbol_table_get(module->exports, c_sv("export="));
    if (!assigned) return module;
    Symbol* target = assigned->flags & SYMBOL_ALIAS ? symbol_resolve_alias(assigned) : assigned;
    if (!target) return module;
    if (target == module || target->is_placeholder || target->is_module || module->exports->parent ||
        module->exports->count < 2 || !module->module
    ) {
        return target;
    }
    return merged_export_equals(module, target);
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
        if (nested && (csview_equals(own[i].name, "default") || csview_equals(own[i].name, "export="))) continue;
        if (!symbol_table_get_own(ctx->target, own[i].name, own[i].hash)) {
            symbol_table_insert(ctx->target, ctx->arena, own[i].name, own[i].hash, own[i].symbol);
        }
        if (type_only_star) {
            i32 at = symbol_table_own_index(&ctx->type_only, own[i].name, own[i].hash);
            if (at < 0) {
                symbol_table_insert(&ctx->type_only, ctx->arena, own[i].name, own[i].hash, own[i].symbol);
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
    for (u32 i = 0; i < count; i++) symbol_table_insert(symbols, &walk->arena, own[i].name, own[i].hash, own[i].symbol);
    free(own);

    SymbolTable nested = {0};
    PortItem const** first_star = nullptr; // Parallel to nested's entries
    u32 first_star_cap = 0;
    // Pairs of (nested entry index, the later star), in order
    struct { u32 entry; PortItem const* star; }* dups = nullptr;
    u32 dup_count = 0, dup_cap = 0;
    for (u32 s = 0; s < star_count; s++) {
        PortItem const* star = module->module->export_stars[s];
        Symbol* target = module_export_equals(binder_module_of_ref(module->binder, star->module_ref));
        SymbolTable const* exported = export_star_visit(walk, target, false);
        if (!exported) continue;
        SymbolEntry* entries = symbol_table_collect(exported, &count);
        for (u32 i = 0; i < count; i++) {
            if (csview_equals(entries[i].name, "default")) continue;
            i32 at = symbol_table_own_index(&nested, entries[i].name, entries[i].hash);
            if (at < 0) {
                symbol_table_insert(&nested, &walk->arena, entries[i].name, entries[i].hash, entries[i].symbol);
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
        if (csview_equals(entry->name, "export=") || symbol_table_get_own(symbols, entry->name, entry->hash)) continue;
        for (u32 d = 0; d < dup_count; d++) {
            if (dups[d].entry != e || !span_valid(dups[d].star->full_span)) continue;
            Span at = span_with_semicolon(dups[d].star->full_span);
            if (!at.source || at.source->is_stdlib) continue;
            error_ctx_error_d(walk->errs, at, 2308, "Module ", first_star[e]->module_ref->span,
                " has already exported a member named '", entry->name,
                "'. Consider explicitly re-exporting to resolve the ambiguity.");
        }
    }
    for (u32 e = 0; e < nested.count; e++) {
        SymbolEntry const* entry = &nested.entries[e];
        if (!symbol_table_get_own(symbols, entry->name, entry->hash)) {
            symbol_table_insert(symbols, &walk->arena, entry->name, entry->hash, entry->symbol);
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
    SymbolTable* table = arena_new(&my binder->arena, SymbolTable);
    ExportStarsCtx ctx = { .target = table, .arena = &my binder->arena };
    extend_export_stars(&ctx, me, false, nullptr);
    // What stays type-only: the names no module reached without a type-only star exports
    for (u32 i = 0; i < ctx.type_only.count; i++) {
        SymbolEntry const* entry = &ctx.type_only.entries[i];
        bool value = false;
        for (u32 m = 0; m < ctx.value_module_count && !value; m++) {
            value = symbol_table_get_own(ctx.value_modules[m]->exports, entry->name, entry->hash);
        }
        if (value) continue;
        if (!my module->type_only_exports) my module->type_only_exports = arena_new(&my binder->arena, SymbolTable);
        SymbolTable* names = my module->type_only_exports;
        symbol_table_insert(names, &my binder->arena, entry->name, entry->hash, entry->symbol);
        if (names->count > my module->type_only_star_cap) {
            u32 cap = my module->type_only_star_cap ? my module->type_only_star_cap * 2 : 8;
            PortItem const** stars = arena_new_n(&my binder->arena, PortItem const*, cap);
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
    ErrorCtx* errs = my alias->scope->errs;
    if (vec_span_is_empty(&item->alias_path)) return nullptr;
    Span first = *vec_span_front(&item->alias_path);
    Symbol* target;
    if (vec_span_size(&item->alias_path) == 1) {
        // (tsc's getSymbolOfPartOfRightHandSideOfImportEquals: a namespace)
        target = scope_resolve(my alias->scope, first.sv, SYMBOL_NAMESPACE);
        if (target == me) target = nullptr;
        if (!target) {
            Symbol const* other = scope_resolve(my alias->scope, first.sv, SYMBOL_VALUE | SYMBOL_TYPE);
            if (other == me) other = nullptr;
            if (!other) {
                error_ctx_error_d(errs, first, 2304, "Cannot find name '", first, "'.");
            } else if (symbol_resolved_flags(other) & SYMBOL_TYPE) {
                error_ctx_error_d(errs, first, 2702, "'", first, "' only refers to a type, but is being used as a namespace here.");
            } else {
                error_ctx_error_d(errs, first, 2503, "Cannot find namespace '", first, "'.");
            }
            return nullptr;
        }
        unused_mark_referenced(my alias->scope, target, SYMBOL_ANY_MEANING);
        vec_symbol_cptr_push(&item->alias_path_symbols, target);
        return resolve_if_pure_alias(target);
    }

    target = scope_resolve(my alias->scope, first.sv, SYMBOL_NAMESPACE);
    if (target == me) target = nullptr;
    if (!target) {
        error_ctx_error_d(errs, first, 2503, "Cannot find namespace '", first, "'.");
        return nullptr;
    }
    unused_mark_referenced(my alias->scope, target, SYMBOL_NAMESPACE);
    vec_symbol_cptr_push(&item->alias_path_symbols, target);
    target = resolve_if_pure_alias(target);
    for (isize i = 1; target && i < vec_span_size(&item->alias_path); i++) {
        Span name = *vec_span_at(&item->alias_path, i);
        bool last = i == vec_span_size(&item->alias_path) - 1;
        Symbol const* container = symbol_for_meaning(target, SYMBOL_NAMESPACE);
        if (!container || container->is_placeholder) return nullptr; // A placeholder: anything goes
        Symbol* found = symbol_get_export(container, name.sv);
        Symbol* next = resolve_if_pure_alias(found);
        if (!next || (!last && !(symbol_resolved_flags(next) & SYMBOL_NAMESPACE) && !next->is_placeholder)) {
            cstr qualified = symbol_fully_qualified_name(container);
            error_ctx_error_d(errs, name, 2694, "Namespace '", qualified, "' has no exported member '", name, "'.");
            cstr_drop(&qualified);
            return nullptr;
        }
        target = next;
        vec_symbol_cptr_push(&item->alias_path_symbols, found);
    }
    return target;
}

// `import * as ns`: a namespace symbol of its own around the module, so that its type prints as `typeof ns` (tsc gets
// there through symbol accessibility, plan 04 stage 3)
static Symbol* make_gather_target(Symbol* me, Symbol* module) {
    PortItem* item = my alias->item;
    Symbol* ret = binder_new_symbol(my binder, port_item_name(item->decl_name), SYMBOL_VALUE_MODULE);
    ret->name_span = item->decl_name;
    ret->gather_module = module;
    symbol_add_decl(ret, (SymbolDecl){
        .kind = SYMBOL_DECL_ALIAS, .flags = SYMBOL_VALUE_MODULE, .node = item,
        .name = item->decl_name, .full = item->full_span,
    });
    return ret;
}

// How tsc names a module in a message: an ambient module by its declared name, a file by the specifier it was
// imported with (TS2305) or by its path without the extension
cstr module_name_for_error(Symbol const* module, ModuleRef const* ref, bool by_specifier) {
    if (!module->module->file) return cstr_from_sv(module->name);
    if (by_specifier) return cstr_d("\"", ref->path, "\"");
    csview path = source_file_display_path(module->module->file);
    static const char* const extensions[] = { ".d.ts", ".d.mts", ".d.cts", ".ts", ".tsx", ".js", ".jsx", ".mts", ".cts", ".mjs", ".cjs", ".json" };
    for (size_t i = 0; i < sizeof(extensions) / sizeof(*extensions); i++) {
        if (csview_ends_with(path, extensions[i])) {
            path.size -= (isize)strlen(extensions[i]);
            break;
        }
    }
    return cstr_d("\"", path, "\"");
}

// tsc's getFullyQualifiedName: the containers too, a file module as its quoted name (`"file".A.B`)
cstr symbol_fully_qualified_name(Symbol const* me) {
    if (my is_module && my module) return module_name_for_error(me, nullptr, false);
    if (!my parent || (my parent->is_module && !my parent->module)) return cstr_from_sv(my name);
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
    SourceFile const* file = symbol->decls[0].full.source ? symbol->decls[0].full.source : symbol->decls[0].name.source;
    return file && !file->is_module;
}

static void report_non_local_export(ErrorCtx* errs, Span name) {
    error_ctx_error_d(errs, name, 2661, "Cannot export '", name, "'. Only local declarations can be exported from a module.");
}

// An ES module's import of a file sure to be an ES module: no synthetic default (tsc's canHaveSyntheticDefault with
// getEmitSyntaxForUsageLocation)
static bool esm_imports_esm(PortItem const* item, SourceFile const* target) {
    SourceFile const* importer = item->decl_name.source;
    if (!importer || item->is_require) return false;
    ModuleKind usage = source_file_emit_module_kind(importer);
    bool esm = usage == MODULE_KIND_PRESERVE || (usage >= MODULE_KIND_ES2015 && usage <= MODULE_KIND_ESNEXT);
    return esm && source_file_implied_format_for_emit(target) == MODULE_KIND_ESNEXT;
}

// An ES module's import of a CommonJS module under node16/nodenext: always a synthetic default
static bool esm_imports_cjs(PortItem const* item, SourceFile const* target) {
    SourceFile const* importer = item->decl_name.source;
    if (!importer || item->is_require) return false;
    ModuleKind usage = source_file_emit_module_kind(importer);
    bool esm = usage == MODULE_KIND_PRESERVE || (usage >= MODULE_KIND_ES2015 && usage <= MODULE_KIND_ESNEXT);
    return esm && source_file_implied_format_for_emit(target) == MODULE_KIND_COMMONJS;
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

// tsc's resolveESModuleSymbol for `import * as ns`: with esModuleInterop, or when an ES module imports a CommonJS
// one, it looks for call and construct signatures on the module's type, resolving its members
static bool namespace_import_resolves_type_members(PortItem const* item) {
    if (item->tag != PORT_IMPORT_GATHER) return false;
    if (compiler_options()->es_module_interop) return true;
    SourceFile const* target = item->module_ref ? item->module_ref->file : nullptr;
    return target && esm_imports_cjs(item, target);
}

// tsc's resolveESModuleSymbol: an ES import of a module whose `export =` names neither a namespace nor a variable
// (a class, a function) only works through the synthetic default (TS2497 on the module name)
static void check_es_module_symbol(PortItem const* item, Symbol const* module, Symbol* assigned, ErrorCtx* errs) {
    if (!assigned || assigned->is_placeholder || !item->module_ref) return;
    // (`module.exports = v` of a value that isn't a name is a ValueModule in tsc's bindModuleExportsAssignment)
    Symbol const* export_equals = symbol_get_export(module, c_sv("export="));
    if (export_equals && export_equals->decl_count && export_equals->decls[0].kind == SYMBOL_DECL_ASSIGNMENT) return;
    Symbol const* target = symbol_resolve_alias(assigned);
    if (!target || target->is_placeholder || target->is_module) return;
    u32 flags = symbol_resolved_flags(assigned);
    if (flags & (SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE | SYMBOL_FUNCTION_SCOPED_VARIABLE | SYMBOL_BLOCK_SCOPED_VARIABLE)) {
        return;
    }
    error_ctx_error_d(errs, item->module_ref->span, 2497, "This module can only be referenced with ECMAScript "
        "imports/exports by turning on the '", compiler_options()->module >= MODULE_KIND_ES2015 ?
        "allowSyntheticDefaultImports" : "esModuleInterop", "' flag and referencing its default export.");
}

void scope_report_non_exported_member(PortItem const* item, Symbol const* module, ErrorCtx* errs) {
    Span name = item->source_name;
    cstr module_name = module_name_for_error(module, item->module_ref, true);
    SourceFile const* file = module->is_module && module->module ? module->module->file : nullptr;
    Symbol* local = file && file->scope ? symbol_table_get(&file->scope->locals, port_item_name(name)) : nullptr;
    Symbol const* export_equals = local ? symbol_get_export(module, c_sv("export=")) : nullptr;
    if (!local || (export_equals && symbol_resolve_alias((Symbol*)export_equals) != symbol_resolve_alias(local))) {
        error_ctx_error_d(errs, name, 2305, "Module '", module_name, "' has no exported member '", name, "'.");
    } else if (export_equals) {
        // (tsc's reportInvalidImportEqualsExportMember)
        CompilerOptions const* options = compiler_options();
        bool interop = options->es_module_interop;
        if (options->module >= MODULE_KIND_ES2015) {
            if (interop) error_ctx_error_d(errs, name, 2595, "'", name, "' can only be imported by using a default import.");
            else error_ctx_error_d(errs, name, 2596, "'", name, "' can only be imported by turning on the 'esModuleInterop' flag and using a default import.");
        } else if (name.source && name.source->is_js) {
            if (interop) error_ctx_error_d(errs, name, 2597, "'", name, "' can only be imported by using a 'require' call or by using a default import.");
            else error_ctx_error_d(errs, name, 2598, "'", name, "' can only be imported by using a 'require' call or by turning on the 'esModuleInterop' flag and using a default import.");
        } else if (interop) {
            error_ctx_error_d(errs, name, 2616, "'", name, "' can only be imported by using 'import ", name, " = require(",
                module_name, ")' or a default import.");
        } else {
            error_ctx_error_d(errs, name, 2617, "'", name, "' can only be imported by using 'import ", name, " = require(",
                module_name, ")' or by turning on the 'esModuleInterop' flag and using a default import.");
        }
    } else {
        // Declared but not exported, or exported under another name
        Symbol const* resolved = symbol_resolve_alias(local);
        SymbolTable const* exports = symbol_exports_table(module);
        csview exported_as = {0};
        for (u32 i = 0; exports && i < exports->count && !exported_as.size; i++) {
            if (symbol_resolve_alias(exports->entries[i].symbol) == resolved) exported_as = exports->entries[i].name;
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

// tsc's getTargetofModuleDefault: `import d from "m"`, `import { default as d }`, `export { default } from "m"`
static Symbol* resolve_default_target(Symbol* me, PortItem* item, Symbol* module, Symbol* assigned, ErrorCtx* errs) {
    // In Node.js a CommonJS module imported into an ES module always has a synthetic default, which overrides a real
    // `default` export (tsc's canHaveSyntheticDefault, getTargetofModuleDefault)
    ModuleKind module_kind = compiler_options()->module;
    SourceFile const* target_file = item->module_ref ? item->module_ref->file : nullptr;
    if (target_file && module_kind >= MODULE_KIND_NODE16 && module_kind <= MODULE_KIND_NODENEXT &&
        esm_imports_cjs(item, target_file)
    ) {
        return assigned ? assigned : module;
    }
    Symbol* found = symbol_get_export(module, c_sv("default"));
    if (!found && assigned) {
        // The default import is what `export =` names, with allowSyntheticDefaultImports or where an ES module
        // imports a CommonJS one (tsc's getTargetofModuleDefault, canHaveSyntheticDefault)
        SourceFile const* file = item->module_ref->file;
        if (!compiler_options()->allow_synthetic_default_imports && !(file && esm_imports_cjs(item, file))) {
            const char* option = compiler_options()->module >= MODULE_KIND_ES2015 ?
                "allowSyntheticDefaultImports" : "esModuleInterop";
            cstr module_name = module_name_for_error(module, item->module_ref, false);
            Error* error = error_ctx_error_d(errs, item->decl_name, 1259, "Module '", module_name,
                "' can only be default-imported using the '", option, "' flag");
            Symbol const* export_equals = symbol_get_export(module, c_sv("export="));
            if (error && export_equals && export_equals->decl_count) {
                error_add_related_d(error, export_equals->decls[0].full, 2594,
                    "This module is declared with 'export =', and can only be used with a default import when "
                    "using the '", option, "' flag.");
            }
            cstr_drop(&module_name);
            return nullptr; // (tsc's exportDefaultSymbol, which there is none of)
        }
        // (`{ default as d }` names the `export=` symbol itself)
        if (item->tag != PORT_IMPORT_DEFAULT) item->source_symbol = symbol_get_export(module, c_sv("export="));
        return assigned;
    }
    if (!found) {
        SourceFile const* file = item->module_ref->file;
        // In Node.js a CommonJS module always has a synthetic default when an ES module imports it (tsc's
        // canHaveSyntheticDefault, before allowSyntheticDefaultImports)
        if (file && esm_imports_cjs(item, file)) return module;
        if (compiler_options()->allow_synthetic_default_imports && file && file->is_commonjs &&
            !symbol_get_export(module, c_sv("__esModule"))
        ) {
            // A CommonJS JavaScript module's synthetic default is the module (tsc's canHaveSyntheticDefault)
            return module;
        }
        if (compiler_options()->allow_synthetic_default_imports && (!file || file->is_declaration) &&
            !symbol_get_export(module, c_sv("__esModule")) && !(file && esm_imports_esm(item, file))
        ) {
            return module; // So is a declaration file's (tsc's canHaveSyntheticDefault)
        }
        // (a specifier naming `default` is tsc's errorNoModuleMemberSymbol)
        if (item->tag != PORT_IMPORT_DEFAULT) {
            scope_report_non_exported_member(item, module, errs);
            return nullptr;
        }
        cstr module_name = module_name_for_error(module, item->module_ref, false);
        Error* error = error_ctx_error_d(errs, item->decl_name, 1192, "Module '", module_name, "' has no default export.");
        // (tsc's reportNonDefaultExport: the first `export *` of a module that has a default)
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
        return nullptr;
    }
    item->source_symbol = found;
    return resolve_if_pure_alias(found);
}

static Symbol* resolve_alias_target(Symbol* me) {
    PortItem* item = my alias->item;
    ErrorCtx* errs = my alias->scope->errs;
    if (my alias->class_expr) return my alias->class_expr->symbol;
    if (!item) {
        // `export default x`, TS2304 comes from the expression
        Symbol* found = scope_resolve(my alias->scope, my alias->name.sv, SYMBOL_ANY_MEANING);
        // (`exports` of a CommonJS module is the module itself before any global, like identifier_expr_lookup_names())
        SourceFile const* file = my alias->scope->file;
        if (file && file->is_js && file->is_commonjs && csview_equals(my alias->name.sv, "exports") &&
            (!found || found == symbol_table_get(&my binder->globals, c_sv("exports"))) && file->scope
        ) {
            found = file->scope->container;
        }
        if (found == me) return nullptr;
        // A local type and an exported value of the name are one symbol in tsc (the local one has the exported one as
        // its exportSymbol): `interface F {}` + `export const F = ...` + `export default F` exports both
        Symbol* container = my alias->scope->container;
        if (found && !(found->flags & SYMBOL_VALUE) && container && container->exports) {
            Symbol* value = symbol_table_get(container->exports, my alias->name.sv);
            if (value && value != found && value != me && (value->flags & SYMBOL_VALUE)) my alias->value_target = value;
        }
        unused_mark_referenced(my alias->scope, found, SYMBOL_ANY_MEANING);
        return resolve_if_pure_alias(found);
    }
    Symbol* module = nullptr;
    Symbol* assigned = nullptr; // What its `export =` names, if it has one
    if (item->tag != PORT_IMPORT_ALIAS && item->tag != PORT_EXPORT_SPECIFIC && item->tag != PORT_EXPORT_AS_NAMESPACE) {
        module = binder_module_of_ref(my binder, item->module_ref);
        if (!module) {
            // No file: TS2307 comes from the statement. (An `@import` tag's is only reported by a use of its names in
            // tsc, whose checkJSDocImportTag doesn't resolve them: not at all here)
            if (item->module_ref->file && !item->jsdoc) {
                error_ctx_error_d(errs, item->module_ref->span, 2306,
                    "File '", source_file_display_path(item->module_ref->file), "' is not a module.");
            }
            return nullptr;
        }
        if (module->is_shorthand_module) return nullptr; // Everything is any
        assigned = module_export_equals(module);
        if (assigned == module) assigned = nullptr;
    }
    switch (item->tag) {
        case PORT_IMPORT_SPECIFIC:
        case PORT_REEXPORT_SPECIFIC: {
            if (!item->source_name.sv.size) return nullptr; // A missing name (`import { "x" }`, the parser's TS1003)
            if (csview_equals(port_item_name(item->source_name), "default")) {
                return resolve_default_target(me, item, module, assigned, errs);
            }
            // (tsc's getExternalModuleMember: `{ default as x }` with allowSyntheticDefaultImports is fine)
            if (!item->value_only && !(csview_equals(port_item_name(item->source_name), "default") &&
                compiler_options()->allow_synthetic_default_imports)
            ) {
                check_es_module_symbol(item, module, assigned, errs);
            }
            Symbol* found = symbol_get_export(module, port_item_name(item->source_name));
            if (!found && assigned) {
                if (assigned->is_placeholder) return nullptr;
                Symbol const* container = symbol_for_meaning(assigned, SYMBOL_NAMESPACE);
                if (container) found = symbol_get_export(container, port_item_name(item->source_name));
                // The properties of the value `export =` names are importable too (tsc's getExternalModuleMember).
                // Whether it has the property is up to the checker: without one the value is the error type
                bool from_value = symbol_resolved_flags(assigned) & SYMBOL_VALUE &&
                    !csview_equals(port_item_name(item->source_name), "default");
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
                    if (!found || item->value_only) return property;
                    my alias->value_target = property;
                }
                if (!found) return nullptr;
            }
            if (item->value_only && found && !(symbol_resolved_flags(found) & SYMBOL_VALUE)) return nullptr;
            if (!found) {
                if (item->value_only) return nullptr;
                scope_report_non_exported_member(item, module, errs);
                return nullptr;
            }
            item->source_symbol = found;
            my alias->type_only_star = module_type_only_star(module, port_item_name(item->source_name));
            return resolve_if_pure_alias(found);
        }

        case PORT_IMPORT_DEFAULT:
            return resolve_default_target(me, item, module, assigned, errs);

        case PORT_EXPORT_SPECIFIC: {
            Symbol* found = scope_resolve(my alias->scope, item->source_name.sv, SYMBOL_ANY_MEANING);
            // (tsc's primitive types and `undefined` aren't symbols its resolveName finds)
            if (symbol_is_builtin_type(found)) found = nullptr;
            if (!found || found == me) {
                if (export_names_global_primitive(item->source_name.sv)) {
                    report_non_local_export(errs, item->source_name);
                } else if (item->source_name.sv.size && (*item->source_name.sv.buf == '"' || *item->source_name.sv.buf == '\'')) {
                    // `export { "x" }` (tsc's checkExportSpecifier skips it: the parser's TS1003)
                } else {
                    error_ctx_error_d(errs, item->source_name, 2304, "Cannot find name '", item->source_name, "'.");
                }
                return nullptr;
            }
            if (is_global_declaration(my binder, found)) report_non_local_export(errs, item->source_name);
            unused_mark_referenced(my alias->scope, found, SYMBOL_ANY_MEANING);
            item->source_symbol = found;
            return resolve_if_pure_alias(found);
        }

        case PORT_IMPORT_GATHER:
        case PORT_REEXPORT_GATHER:
            if (!item->is_require) check_es_module_symbol(item, module, assigned, errs);
            if (assigned) return assigned;
            if (namespace_import_resolves_type_members(item)) resolve_module_type_members(module);
            return make_gather_target(me, module);

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

Symbol* symbol_resolve_alias(Symbol* me) {
    if (!me || !(my flags & SYMBOL_ALIAS)) return me;
    switch (my alias->state) {
        case ALIAS_DONE: return my alias->target;
        case ALIAS_IN_FLIGHT:
        case ALIAS_CIRCULAR:
            // (tsc's resolveAlias: the inner visit makes the target unknown, the outer one reports)
            my alias->state = ALIAS_CIRCULAR;
            return nullptr;
        case ALIAS_UNRESOLVED: break;
    }
    if (my binder->frozen) log_error("Alias '", my name, "' resolved while typechecking: the checkers share it");
    my alias->state = ALIAS_IN_FLIGHT;
    Symbol* target = resolve_alias_target(me);
    // (`export default x` looks its name up in a scope that has it)
    if (my alias->state == ALIAS_CIRCULAR && my alias->item) {
        // (Import-equals declarations are reported on the whole statement)
        PortItem const* item = my alias->item;
        Span at = item->tag == PORT_IMPORT_ALIAS || item->is_require ? span_with_semicolon(item->full_span) : item->decl_name;
        error_ctx_error_d(my alias->scope->errs, at, 2303, "Circular definition of import alias '", my name, "'.");
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
        flags |= target ? resolved_flags(target, depth + 1) : SYMBOL_ANY_MEANING;
        if (my alias->value_target) flags |= my alias->value_target->flags;
    }
    return flags;
}

u32 symbol_resolved_flags(Symbol const* me) {
    return resolved_flags(me, 0);
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

PortItem const* symbol_type_only_declaration(Symbol const* me) {
    for (int depth = 0; me && depth < ALIAS_CHAIN_MAX; depth++) {
        if (!(my flags & SYMBOL_ALIAS) || !my alias || !my alias->item) return nullptr;
        symbol_resolve_alias((Symbol*)me); // (what sets source_symbol)
        PortItem const* item = my alias->item;
        if (item->type_only) return item;
        if (my alias->type_only_star) return my alias->type_only_star;
        me = item->source_symbol;
        if (me && me->flags & SYMBOL_VALUE) return nullptr; // Merged with a value along the way
    }
    return nullptr;
}

// Module augmentations (tsc's mergeModuleAugmentation)

void binder_add_augmentation(Binder* me, NamespaceDecl* decl) {
    if (my augmentation_count == my augmentation_cap) {
        my augmentation_cap = my augmentation_cap ? my augmentation_cap * 2 : 4;
        my augmentations = realloc(my augmentations, sizeof(NamespaceDecl*) * my augmentation_cap);
    }
    my augmentations[my augmentation_count++] = decl;
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
        report_conflicts(errs, target, source->decls[0].name, source->flags, true);
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
        for (u32 i = 0; i < count; i++) merge_into_exports(binder, target, entries[i].name, entries[i].symbol, errs);
        free(entries);
        source->exports = target->exports;
    }
}

// Adds source to the exports of owner under name, merging it into what is there already: into the target of an alias,
// into what comes through `export *`
static void merge_into_exports(Binder* binder, Symbol* owner, csview name, Symbol* source, ErrorCtx* errs) {
    u64 hash = symbol_name_hash(name);
    Symbol* existing = symbol_table_get_own(owner->exports, name, hash);
    if (!existing && owner->is_module && owner->module->export_star_count) {
        existing = (Symbol*)symbol_table_lookup(module_resolved_exports(owner), name, hash);
    }
    if (!existing) {
        symbol_table_insert(owner->exports, &binder->arena, name, hash, source);
        if (owner->is_module && owner->module->resolved_exports_state == ALIAS_DONE) {
            symbol_table_insert(owner->module->resolved_exports, &binder->arena, name, hash, source);
        }
        source->parent = owner;
        return;
    }
    if (symbol_is_pure_alias(existing)) {
        existing = symbol_resolve_alias(existing);
        if (!existing || existing->is_placeholder) return;
    }
    merge_symbol(binder, existing, source, errs);
}

// Whether merging an augmentation's name needs aliases resolved: then it waits until the others are merged, so that
// what resolves through the module sees them
static bool augmentation_entry_needs_resolving(Symbol const* module, csview name) {
    Symbol const* existing = symbol_table_get(module->exports, name);
    if (existing) return (existing->flags & SYMBOL_ALIAS) != 0;
    return module->is_module && module->module->export_star_count;
}

// The module an augmentation adds to (the module symbol itself, not its `export =`)
static Symbol* augmented_module(Binder* me, NamespaceDecl* decl) {
    ModuleRef* ref = decl->module_ref;
    Symbol* ret = nullptr;
    if (ref->file) ret = ref->file->scope ? ref->file->scope->container : nullptr;
    if (!ret) {
        ret = binder_module_of_ref(me, ref);
        if (ret && ret->is_module && !ret->module->file && memchr(ret->name.buf, '*', (size_t)ret->name.size)) ret = nullptr;
    }
    // TODO: TS2664 for an augmentation of a module nobody declares
    if (!ret) ret = binder_ambient_module(me, cstr_sv(&ref->path), decl->name);
    return ret;
}

static void merge_augmentations(Binder* me) {
    // tsc's resolveExternalModule for an augmentation: a name that leads to JavaScript (no declarations) and no
    // ambient module can't be augmented
    for (u32 i = 0; i < my augmentation_count; i++) {
        NamespaceDecl* decl = my augmentations[i];
        ModuleRef* ref = decl->module_ref;
        if (ref->file || ref->failure != MODULE_REF_IS_JS || binder_module_of_ref(me, ref)) continue;
        error_ctx_error_d(decl->inner_scope->errs, ref->span, 2665, "Invalid module name in augmentation. Module '",
            ref->path, "' resolves to an untyped module at '", ref->resolved_path, "', which cannot be augmented.");
    }
    // First what merges without resolving anything, then (in the same order) what needs aliases or `export *`
    // resolved, and augmentations of an `export =` module, which go into what it assigns
    for (int pass = 0; pass < 2; pass++) {
        for (u32 i = 0; i < my augmentation_count; i++) {
            NamespaceDecl* decl = my augmentations[i];
            Symbol* augmentation = decl->symbol;
            Symbol* module = augmented_module(me, decl);
            ErrorCtx* errs = decl->inner_scope->errs;
            bool export_equals = symbol_table_get(module->exports, c_sv("export=")) != nullptr;
            if (export_equals) {
                if (pass == 0) continue;
                Symbol* assigned = module_export_equals(module);
                // TODO: TS2671 when it isn't a namespace
                if (!assigned || assigned->is_placeholder || !(assigned->flags & SYMBOL_NAMESPACE)) continue;
                if (assigned->binder != me) continue; // TODO: a namespace of the shared lib program
                module = assigned;
                symbol_ensure_exports(module);
            }
            u32 count;
            SymbolEntry* entries = symbol_table_collect(augmentation->exports, &count);
            for (u32 j = 0; j < count; j++) {
                bool late = export_equals || augmentation_entry_needs_resolving(module, entries[j].name);
                if (late != (pass == 1)) continue;
                merge_into_exports(me, module, entries[j].name, entries[j].symbol, errs);
            }
            free(entries);
        }
    }
    // Every block of an augmentation sees the module's exports, and `declare module "m"` is the module
    for (u32 i = 0; i < my augmentation_count; i++) {
        NamespaceDecl* decl = my augmentations[i];
        Symbol* module = augmented_module(me, decl);
        Symbol* assigned = module_export_equals(module);
        if (assigned && assigned != module && !assigned->is_placeholder && assigned->flags & SYMBOL_NAMESPACE &&
            assigned->binder == me
        ) {
            module = assigned;
        }
        decl->symbol->exports = module->exports;
        decl->symbol = module;
        symbol_add_decl(module, (SymbolDecl){
            .kind = SYMBOL_DECL_NAMESPACE, .node = decl, .name = decl->name, .full = decl->full_span,
        });
    }
}

void binder_resolve_remaining_aliases(Binder* me) {
    for (u32 i = 0; i < my symbol_count; i++) {
        Symbol* symbol = my symbols[i];
        if (symbol->flags & SYMBOL_ALIAS && symbol->alias->state == ALIAS_UNRESOLVED) symbol_resolve_alias(symbol);
    }
}

void binder_link(Binder* me) {
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
        if (!symbol->alias->item || !target || target->is_placeholder) continue;
        // In tsc an exported declaration also has a local symbol, which an import of the same name conflicts with
        u32 local_flags = symbol->flags;
        Symbol const* container = symbol->alias->scope->container;
        if (container && !port_item_declares_export(symbol->alias->item)) {
            Symbol const* exported = symbol_table_get(container->exports, symbol->name);
            if (exported && exported != symbol) local_flags |= exported->flags & ~(u32)SYMBOL_ALIAS;
        }
        if (!(local_flags & SYMBOL_ANY_MEANING)) continue;
        // An alias merged with local declarations: fine as long as they mean different things (tsc: checkAliasSymbol)
        u32 target_flags = symbol_resolved_flags(target);
        u32 excluded = (target_flags & SYMBOL_VALUE ? SYMBOL_VALUE : 0) |
            (target_flags & SYMBOL_TYPE ? SYMBOL_TYPE : 0) |
            (target_flags & SYMBOL_NAMESPACE ? SYMBOL_NAMESPACE : 0);
        if (!(local_flags & excluded)) continue;
        PortItem const* item = symbol->alias->item;
        ErrorCtx* errs = symbol->alias->scope->errs;
        if (port_item_is_export(item)) {
            error_ctx_error_d(errs, item->full_span, 2484,
                "Export declaration conflicts with exported declaration of '", item->decl_name, "'.");
        } else {
            // tsc reports on the import specifier (`a as b`), or the whole `import a = b.c`
            Span at = item->decl_name;
            if (item->tag == PORT_IMPORT_ALIAS) at = span_with_semicolon(item->full_span);
            if (item->tag == PORT_IMPORT_SPECIFIC && span_valid(item->source_name)) {
                at = span_between(item->source_name, item->decl_name);
            }
            error_ctx_error_d(errs, at, 2440,
                "Import declaration conflicts with local declaration of '", item->decl_name, "'.");
        }
    }
    // What module_export_equals() makes: now, not by whichever checker asks first
    for (u32 i = 0, count = my symbol_count; i < count; i++) {
        if (my symbols[i]->is_module) module_export_equals(my symbols[i]);
    }
}

// Lookup

static bool is_export_specifier_alias(Symbol const* me) {
    return my flags == SYMBOL_ALIAS && my alias->item && port_item_is_export(my alias->item);
}

static bool symbol_has_meaning(Symbol const* me, u32 meaning) {
    if (my flags & meaning || my is_placeholder) return true;
    return my flags & SYMBOL_ALIAS && symbol_resolved_flags(me) & meaning;
}

bool scope_declares_symbol(Scope const* me, Symbol const* symbol) {
    u64 hash = symbol_name_hash(symbol->name);
    SymbolTable const* table = my kind == SCOPE_GLOBAL ? &my binder->globals : &my locals;
    if ((table->count || table->parent) && symbol_table_lookup(table, symbol->name, hash) == symbol) return true;
    return my container && my container->exports && symbol_table_lookup(my container->exports, symbol->name, hash) == symbol;
}

// tsc's levenshteinWithMax: case differences cost 0.1. A negative result: more than max
static double levenshtein_with_max(csview s1, csview s2, double max) {
    isize n = s2.size;
    double stack[2][64] = {0};
    double* previous = n + 1 <= 64 ? stack[0] : malloc(sizeof(double) * (size_t)(n + 1));
    double* current = n + 1 <= 64 ? stack[1] : malloc(sizeof(double) * (size_t)(n + 1));
    double big = max + 0.01;
    for (isize i = 0; i <= n; i++) previous[i] = (double)i;
    double result = -1;
    bool gave_up = false;
    for (isize i = 1; i <= s1.size && !gave_up; i++) {
        char c1 = s1.buf[i - 1];
        isize min_j = (isize)ceil((double)i > max ? (double)i - max : 1);
        isize max_j = (isize)floor((double)n > max + (double)i ? max + (double)i : (double)n);
        current[0] = (double)i;
        double col_min = (double)i;
        for (isize j = 1; j < min_j; j++) current[j] = big;
        for (isize j = min_j; j <= max_j; j++) {
            char c2 = s2.buf[j - 1];
            double substitution = tolower((unsigned char)c1) == tolower((unsigned char)c2) ? previous[j - 1] + 0.1 : previous[j - 1] + 2;
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
    isize maximum_length_difference = name.size * 34 / 100 > 2 ? name.size * 34 / 100 : 2;
    double best_distance = (double)(name.size * 4 / 10) + 1;
    isize best = -1;
    for (isize i = 0; i < count; i++) {
        csview candidate = names[i];
        if (!candidate.buf) continue;
        isize difference = candidate.size > name.size ? candidate.size - name.size : name.size - candidate.size;
        if (difference > maximum_length_difference || csview_equals_sv(candidate, name)) continue;
        // Short names only when they differ by case
        if (candidate.size < 3 && !equals_ignoring_case(candidate, name)) continue;
        double distance = levenshtein_with_max(name, candidate, best_distance - 0.1);
        if (distance < 0) continue;
        best_distance = distance;
        best = i;
    }
    return best;
}

typedef struct Suggestion {
    csview name;
    Symbol const* symbol; // Null for a primitive (`string` for `String`)
} Suggestion;

// tsc's getSuggestionForSymbolNameLookup on one table
static bool suggest_in_table(SymbolTable const* table, bool globals, csview name, u32 meaning, Suggestion* out) {
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
        csview candidate = entries[i].name;
        // (Keyword types like `unknown` are no symbols in tsc)
        bool builtin = symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_BUILTIN;
        bool fits = !symbol->is_placeholder && !builtin && candidate.size && candidate.buf[0] != '"' && candidate.buf[0] != '\'' &&
            ((symbol->flags & meaning) || ((symbol->flags & SYMBOL_ALIAS) && (symbol_resolved_flags(symbol) & meaning)));
        names[extra + i] = fits ? candidate : (csview){0};
    }
    isize best = spelling_suggestion(name, names, count + extra);
    if (best >= 0) *out = (Suggestion){ names[best], best >= extra ? entries[best - extra].symbol : nullptr };
    free(names);
    free(entries);
    return best >= 0;
}

// tsc's resolveNameForSymbolSuggestion: the innermost table with a close enough name decides
static bool scope_suggest(Scope const* me, csview name, u32 meaning, Suggestion* out) {
    for (Scope const* scope = me; scope; scope = scope->parent) {
        bool globals = scope->kind == SCOPE_GLOBAL;
        SymbolTable const* table = globals ? &scope->binder->globals : &scope->locals;
        if ((table->count || table->parent || globals) && suggest_in_table(table, globals, name, meaning, out)) return true;
        if (scope->container && scope->container->exports && scope->kind != SCOPE_GLOBAL &&
            suggest_in_table(scope->container->exports, false, name, meaning, out)
        ) {
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
    static char const* const test_runner[] = { "describe", "suite", "it", "test" };
    static char const* const node[] = { "process", "require", "Buffer", "module" };
    static char const* const later_lib[] = {
        "Map", "Set", "Promise", "Symbol", "WeakMap", "WeakSet", "Iterator", "AsyncIterator", "SharedArrayBuffer",
        "Atomics", "AsyncIterable", "AsyncIterableIterator", "AsyncGenerator", "AsyncGeneratorFunction", "BigInt",
        "Reflect", "BigInt64Array", "BigUint64Array",
    };
    bool types = compiler_options()->types_set;
    csview n = name.sv;
    if (name_is_one_of(n, dom, 2)) {
        error_ctx_error_d(errs, name, 2584, "Cannot find name '", name, "'. Do you need to change your target library? "
            "Try changing the 'lib' compiler option to include 'dom'.");
    } else if (csview_equals(n, "$")) {
        error_ctx_error_d(errs, name, types ? 2592 : 2581, "Cannot find name '", name, "'. Do you need to install type "
            "definitions for jQuery? Try `npm i --save-dev @types/jquery`",
            types ? " and then add 'jquery' to the types field in your tsconfig." : ".");
    } else if (name_is_one_of(n, test_runner, 4)) {
        error_ctx_error_d(errs, name, types ? 2593 : 2582, "Cannot find name '", name, "'. Do you need to install type "
            "definitions for a test runner? Try `npm i --save-dev @types/jest` or `npm i --save-dev @types/mocha`",
            types ? " and then add 'jest' or 'mocha' to the types field in your tsconfig." : ".");
    } else if (name_is_one_of(n, node, 4)) {
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

void scope_report_not_found(Scope const* me, Span name, u32 meaning, bool shorthand, ErrorCtx* errs) {
    Binder* binder = my binder;
    // A global of a later lib is said to be so rather than misspelled
    char const* lib = lib_feature_of_global(name.sv);
    if (lib) {
        binder->suggestion_count++;
        error_cannot_find_name(errs, name, lib, shorthand);
        return;
    }
    if (scope_report_misspelled(me, name, name.sv, meaning, errs)) return;
    error_cannot_find_name(errs, name, nullptr, shorthand);
}

bool scope_report_misspelled(Scope const* me, Span at, csview name, u32 meaning, ErrorCtx* errs) {
    Suggestion suggestion = {0};
    if (my binder->suggestion_count++ >= 10 || !scope_suggest(me, name, meaning, &suggestion)) return false;
    Error* error = error_ctx_error_d(errs, at, 2552, "Cannot find name '", name, "'. Did you mean '", suggestion.name, "'?");
    Symbol const* symbol = suggestion.symbol;
    for (u32 i = 0; symbol && i < symbol->decl_count; i++) {
        if (!(symbol->decls[i].flags & SYMBOL_VALUE)) continue;
        error_add_related_d(error, symbol->decls[i].name, 2728, "'", suggestion.name, "' is declared here.");
        break;
    }
    return true;
}

typedef struct PendingNotFound {
    Scope const* scope;
    Span name;
    bool shorthand;
    ErrorCtx* errs;
} PendingNotFound;

void scope_report_value_not_found(Scope const* me, Span name, bool shorthand, ErrorCtx* errs) {
    bool in_class = false;
    for (Scope const* scope = me; scope && !in_class; scope = scope->parent) in_class = scope->this_ref.class_decl;
    if (!in_class || (my flow && my flow->in_type_query)) {
        ClassItem const* field = in_class ? scope_invalid_initializer_field(me, name.sv, nullptr) : nullptr;
        if (field) {
            scope_report_invalid_initializer_reference(field, name, errs);
            return;
        }
        scope_report_not_found(me, name, SYMBOL_VALUE, shorthand, errs);
        return;
    }
    Binder* binder = my binder;
    if (binder->pending_not_found_count == binder->pending_not_found_cap) {
        binder->pending_not_found_cap = binder->pending_not_found_cap ? binder->pending_not_found_cap * 2 : 8;
        binder->pending_not_found = realloc(binder->pending_not_found, sizeof(PendingNotFound) * binder->pending_not_found_cap);
        panic_if(!binder->pending_not_found, "out of memory");
    }
    binder->pending_not_found[binder->pending_not_found_count++] = (PendingNotFound){ me, name, shorthand, errs };
}

// tsc's checkAndReportErrorForMissingPrefix: from the `this` container (a class member, or a function in one) out, a
// static member of a class around it, or an instance member of the class it is directly a member of
static bool report_missing_prefix(Scope const* me, Span name, ErrorCtx* errs) {
    Checker* checker = current_checker;
    Atom const* atom = checker_atom(checker, name.sv);
    bool container_found = false, direct = false;
    for (Scope const* scope = me; scope; scope = scope->parent) {
        ThisKind kind = scope->this_ref.kind;
        if (!kind) continue;
        bool first = !container_found;
        container_found = true;
        ClassDecl const* class_decl = scope->this_ref.class_decl;
        if (!class_decl || !class_decl->symbol) continue;
        direct = first;
        if (ty_get_prop(checker, ty_static_type_of(checker, class_decl->symbol), atom)) {
            error_ctx_error_d(errs, name, 2662, "Cannot find name '", name, "'. Did you mean the static member '",
                class_decl->symbol->name, ".", name, "'?");
            return true;
        }
        if (direct && kind == THIS_CLASS_INSTANCE && ty_get_prop(checker, ty_declared_type_of(checker, class_decl->symbol), atom)) {
            error_ctx_error_d(errs, name, 2663, "Cannot find name '", name, "'. Did you mean the instance member 'this.", name, "'?");
            return true;
        }
    }
    return false;
}

void scope_report_missing_prefixes(Binder* me) {
    for (u32 i = 0; i < my pending_not_found_count; i++) {
        PendingNotFound const* pending = &my pending_not_found[i];
        if (report_missing_prefix(pending->scope, pending->name, pending->errs)) continue;
        ClassItem const* field = scope_invalid_initializer_field(pending->scope, pending->name.sv, nullptr);
        if (field) {
            scope_report_invalid_initializer_reference(field, pending->name, pending->errs);
        } else {
            scope_report_not_found(pending->scope, pending->name, SYMBOL_VALUE, pending->shorthand, pending->errs);
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
        if (declaration && span_valid(decl->name)) return decl->name.sv;
    }
    return (csview){0};
}

Symbol* scope_resolve(Scope const* me, csview name, u32 meaning) {
    u64 hash = symbol_name_hash(name);
    bool from_parameters = false, from_return_type = false;
    bool arguments = (meaning & SYMBOL_VARIABLE) && csview_equals(name, "arguments");
    bool arrows_have_arguments = compiler_options()->target < SCRIPT_TARGET_ES2015;
    bool jsdoc_parameter_type = false;
    for (Scope const* scope = me; scope; scope = scope->parent) {
        jsdoc_parameter_type |= scope->jsdoc_parameter_type;
        SymbolTable const* table = scope->kind == SCOPE_GLOBAL ? &scope->binder->globals : &scope->locals;
        if (table->count || table->parent) {
            Symbol* found = symbol_table_lookup(table, name, hash);
            bool body_variable = from_parameters && found && (found->flags & SYMBOL_VARIABLE) && !found->is_parameter &&
                compiler_options()->target >= SCRIPT_TARGET_ES2015;
            if (jsdoc_parameter_type && scope->kind == SCOPE_FUNCTION && found && (found->flags & SYMBOL_FUNCTION_SCOPED_VARIABLE) &&
                !(meaning & ~SYMBOL_VALUE & found->flags)
            ) {
                found = nullptr;
            }
            if (from_return_type && found && !found->is_parameter) found = nullptr;
            if (found && !body_variable && symbol_has_meaning(found, meaning)) return found;
        }
        if (scope->kind == SCOPE_FUNCTION) jsdoc_parameter_type = false;
        if (arguments && scope->kind == SCOPE_FUNCTION && scope->function && !scope->function->is_type &&
            (arrows_have_arguments || !span_valid(scope->function->arrow_span))
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
            if (found && found->is_default_export && symbol_has_meaning(found, meaning) &&
                csview_equals_sv(symbol_default_export_local_name(found), name)
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
    while (with_locals && !with_locals->constructor_local_count) with_locals = with_locals->parent;
    if (!with_locals) return nullptr;
    ClassItem const* ret = nullptr;
    for (Scope const* scope = me; scope; scope = scope->parent) {
        if (found && scope_declares_symbol(scope, found)) break;
        // (tsc keeps the outermost one when fields are nested in class expressions)
        for (u32 i = 0; i < scope->constructor_local_count; i++) {
            if (csview_equals_sv(scope->constructor_locals[i].sv, name)) {
                ret = scope->instance_field;
                break;
            }
        }
    }
    return ret;
}

void scope_report_invalid_initializer_reference(ClassItem const* field, Span name, ErrorCtx* errs) {
    Span type = field->field.has_explicit_type ? field->field.explicit_type_span : span_null;
    bool in_type = span_valid(type) && name.source == type.source && span_start_ptr(name) >= span_start_ptr(type) &&
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
    return scope_get(me, name, SYMBOL_VALUE, found);
}

Symbol const* scope_get_type(Scope const* me, csview name, Symbol const** found) {
    return scope_get(me, name, SYMBOL_TYPE, found);
}

Symbol const* scope_get_namespace(Scope const* me, csview name, Symbol const** found) {
    return scope_get(me, name, SYMBOL_NAMESPACE, found);
}

ThisRef scope_get_this(Scope const* me, bool type_else_value) {
    for (Scope const* scope = me; scope; scope = scope->parent) {
        ThisKind kind = scope->this_ref.kind;
        if (!kind) continue;
        bool is_class = kind == THIS_CLASS_INSTANCE || kind == THIS_CLASS_STATIC || kind == THIS_INTERFACE ||
            kind == THIS_TYPE_LITERAL;
        if (!type_else_value || is_class) return scope->this_ref;
        // A `this` type in a function that isn't a method of a class (tsc's getThisContainer stops there) has nothing
        // to be about
        if (scope->this_ref.type_boundary) return (ThisRef){ .kind = THIS_TYPE_LITERAL };
    }
    return (ThisRef){0};
}

struct ClassDecl const* scope_enclosing_class(Scope const* me) {
    for (Scope const* scope = me; scope; scope = scope->parent) {
        if (scope->this_ref.class_decl) return scope->this_ref.class_decl;
    }
    return nullptr;
}

FunctionImpl const* symbol_decl_js_function(SymbolDecl const* decl) {
    if (!decl->full.source || !decl->full.source->is_js) return nullptr;
    Expr const* value = nullptr;
    switch (decl->kind) {
        case SYMBOL_DECL_FUNCTION: return decl->node;
        case SYMBOL_DECL_VARIABLE: {
            Binding const* binding = decl->node;
            if (binding->parent || binding->tag != BINDING_IDENTIFIER) return nullptr;
            value = &((InitializedBinding const*)binding)->value;
        }   break;
        case SYMBOL_DECL_ASSIGNMENT: value = decl->node; break;
        default: return nullptr;
    }
    value = defaulted_initializer(value);
    return value->tag == EXPR_FUNCTION && !span_valid(value->function->arrow_span) ? value->function : nullptr;
}

Generic const* symbol_type_generic(Symbol const* me) {
    if (!me) return nullptr;
    for (u32 i = 0; i < my decl_count; i++) {
        Generic const* generic = nullptr;
        switch (my decls[i].kind) {
            case SYMBOL_DECL_CLASS: generic = &((ClassDecl const*)my decls[i].node)->generic; break;
            case SYMBOL_DECL_INTERFACE: generic = &((InterfaceDecl const*)my decls[i].node)->generic; break;
            case SYMBOL_DECL_TYPE_ALIAS: generic = &((TypeDecl const*)my decls[i].node)->generic; break;
            // A JavaScript constructor function's `@template`s are its class's (tsc's
            // getLocalTypeParametersOfClassOrInterfaceOrTypeAlias: isJSConstructor)
            case SYMBOL_DECL_FUNCTION:
            case SYMBOL_DECL_VARIABLE:
            case SYMBOL_DECL_ASSIGNMENT: {
                if (!(my flags & SYMBOL_CLASS)) continue;
                FunctionImpl const* function = symbol_decl_js_function(&my decls[i]);
                if (!function) continue;
                generic = &function->generic;
            }   break;
            default: continue;
        }
        return generic_empty(generic) ? nullptr : generic;
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

// `a || v` / `a ?? v` (a defaulted initializer, tsc's getDefaultedExpandoInitializer): v, else the expression itself
static Expr const* defaulted_initializer(Expr const* value) {
    if (value->tag == EXPR_BINARY_OP && !expr_is_perenthisized(value) &&
        (value->binary_op->op == BINARY_OP_OR_OR || value->binary_op->op == BINARY_OP_EH_EH)
    ) {
        return &value->binary_op->right;
    }
    return value;
}

// `window.x`, `self.x`, `global.x`, `this.x` for a name x (tsc's isSameEntityName)
static bool is_global_like_access(Expr const* access, Span name) {
    Span access_name = expr_static_property_name(access);
    if (!span_valid(access_name) || !csview_equals_sv(access_name.sv, name.sv)) return false;
    Expr const* object = &access->property->expr;
    if (object->tag != EXPR_IDENTIFIER || expr_is_perenthisized(object)) return false;
    if (object->identifier.keyword) return object->identifier.keyword == KEYWORD_IDENTIFIER_THIS;
    return span_equals(object->identifier.name, "window") || span_equals(object->identifier.name, "self") ||
        span_equals(object->identifier.name, "global");
}

// The expression a variable of this initializer is an expando container of (tsc's getDeclaredExpandoInitializer):
// a function or arrow function, in JavaScript also a class expression or `{}`, possibly defaulted (`var ns = ns || {}`)
Expr* expando_initializer(Expr* init, Span name, bool js) {
    // `Object.defineProperty(o, "x", { value: {} })`: the value (tsc's getAssignedExpandoInitializer, hasExpandoValueProperty)
    if (js && init->tag == EXPR_CALL && expr_is_define_property_call(init, nullptr)) {
        Expr const* descriptor = vec_expr_at(&init->call->args, 2);
        if (descriptor->tag != EXPR_OBJECT) return nullptr;
        for_each (i, vec_object_expr_item, &descriptor->object) {
            if (i->tag != OBJECT_ITEM_PROPERTY || i->computed_key || !span_equals(i->name_span, "value")) continue;
            Expr* ret = expando_initializer((Expr*)&i->value, span_null, js);
            if (ret) return ret;
        }
        return nullptr;
    }
    if (init->tag == EXPR_BINARY_OP && !expr_is_perenthisized(init) &&
        (init->binary_op->op == BINARY_OP_OR_OR || init->binary_op->op == BINARY_OP_EH_EH)
    ) {
        // `var ns = ns || {}`, `ns.a = ns.a || {}` (tsc's getDefaultedExpandoInitializer: the same name on the left)
        Expr const* left = &init->binary_op->left;
        bool same = span_valid(name) ? (left->tag == EXPR_IDENTIFIER && csview_equals_sv(left->identifier.name.sv, name.sv)) ||
            is_global_like_access(left, name) : left->tag == EXPR_PROPERTY || left->tag == EXPR_IDENTIFIER;
        if (!js || !same) return nullptr;
        init = &init->binary_op->right;
    }
    while (init->tag == EXPR_BINARY_OP && init->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(init)) {
        init = &init->binary_op->right;
    }
    switch (init->tag) {
        case EXPR_FUNCTION: return init;
        case EXPR_CLASS_DECL: return js ? init : nullptr;
        case EXPR_OBJECT: return js && !vec_object_expr_item_size(&init->object) ? init : nullptr;
        default: return nullptr;
    }
}

static Symbol* lookup_name_for_property_access(Scope* me, csview name);

// What an entity name `a` or `a.b.c` names where an assignment to one of its properties is (tsc's
// lookupSymbolForPropertyAccess): a name in the block, then in its function (or file); a property of a namespace-like
// symbol's exports. Null for anything else
static Symbol* lookup_for_property_access(Scope* me, Expr const* entity) {
    if (expr_is_perenthisized(entity)) return nullptr;
    if (entity->tag == EXPR_PROPERTY) {
        Span name = expr_static_property_name(entity);
        Symbol* object = span_valid(name) ? lookup_for_property_access(me, &entity->property->expr) : nullptr;
        return object && object->exports && !object->is_module ? symbol_table_get(object->exports, name.sv) : nullptr;
    }
    if (entity->tag != EXPR_IDENTIFIER || entity->identifier.keyword) return nullptr;
    Symbol* found = lookup_name_for_property_access(me, entity->identifier.name.sv);
    // The local `f` of `export default function f` is the default export
    return found && found->export_symbol ? found->export_symbol : found;
}

static Symbol* lookup_name_for_property_access(Scope* me, csview name) {
    u64 hash = symbol_name_hash(name);
    for (Scope* scope = me; scope; scope = scope->parent) {
        if (scope != me && !scope_is_var_container(scope)) continue;
        bool global = scope->kind == SCOPE_SCRIPT || scope->kind == SCOPE_GLOBAL_AUGMENTATION;
        Symbol* found = symbol_table_lookup(global ? &my binder->globals : &scope->locals, name, hash);
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
        if (found || scope_is_var_container(scope)) return found;
    }
    return nullptr;
}

// Whether a symbol can have expandos (tsc's isExpandoSymbol, and in TypeScript isFunctionSymbol). `init_ret`: a
// variable's (or JavaScript member's) expando initializer. tsc binds a file before globals merge: what counts is the
// first value declaration in `file` (the valueDeclaration of the file's own symbol)
static bool symbol_is_expando_host(Symbol const* found, SourceFile const* file, bool js, Expr** init_ret) {
    *init_ret = nullptr;
    SymbolDecl const* value = nullptr;
    for (u32 i = 0; i < found->decl_count && !value; i++) {
        if ((found->decls[i].flags & SYMBOL_VALUE) && found->decls[i].full.source == file) value = &found->decls[i];
    }
    if (!value) return false;
    switch (value->kind) {
        case SYMBOL_DECL_FUNCTION: return true;
        case SYMBOL_DECL_CLASS:
        case SYMBOL_DECL_NAMESPACE:
        case SYMBOL_DECL_ENUM:
        case SYMBOL_DECL_EXPANDO: // A global namespace a JavaScript expando declared
            return js;
        case SYMBOL_DECL_VARIABLE: {
            Binding const* binding = value->node;
            if (binding->parent || binding->tag != BINDING_IDENTIFIER) return false;
            InitializedBinding* initialized = (InitializedBinding*)binding;
            Expr* init = expando_initializer(&initialized->value, binding->content_span, js);
            if (!init) return false;
            // (An annotated variable has the type it says, but its initializer gets the members all the same)
            *init_ret = init;
            return true;
        }
        case SYMBOL_DECL_ASSIGNMENT: {
            // JavaScript `ns.a = {}` (or a function, a class): `ns.a.b = v` declares b in it
            if (!js) return false;
            Expr* init = expando_initializer((Expr*)value->node, span_null, js);
            if (!init) return false;
            *init_ret = init;
            return true;
        }
        default: return false;
    }
}

static void set_expando_initializer_host(Expr* init, Symbol* host) {
    if (init && init->tag == EXPR_FUNCTION) init->function->expando_host = host;
    if (init && init->tag == EXPR_CLASS_DECL) init->class_decl->expando_host = host;
}

// What `f` of `f.x = v` names, if it can have expandos. `init_ret`: a variable's expando initializer
static Symbol* expando_host(Scope* me, Expr const* entity, Expr** init_ret) {
    *init_ret = nullptr;
    SourceFile const* file = my file;
    bool js = file && file->is_js;
    Symbol* found = lookup_for_property_access(me, entity);
    if (!found || found->is_placeholder || (found->flags & SYMBOL_ALIAS)) return nullptr;
    // A global declared in another file isn't this file's to extend (tsc looks in the file's locals): see
    // global_expando_namespace()
    bool here = false;
    for (u32 i = 0; i < found->decl_count && !here; i++) here = found->decls[i].full.source == file;
    if (!here) return nullptr;
    return symbol_is_expando_host(found, file, js, init_ret) ? found : nullptr;
}

// Whether the entity name of a top-level JavaScript `a.b.x = v` starts with a name the file doesn't declare, which
// makes it a global namespace (tsc's bindPotentiallyMissingNamespaces: file.jsGlobalAugmentations)
static bool is_global_expando_entity(Scope* me, Expr const* target) {
    Expr const* entity = target;
    Expr const* second = nullptr;
    while (entity->tag == EXPR_PROPERTY && !expr_is_perenthisized(entity)) {
        Span name = expr_static_property_name(entity);
        if (!span_valid(name) || name.sv.buf[0] == '#') return false;
        second = entity;
        entity = &entity->property->expr;
    }
    if (expr_is_perenthisized(entity) || entity->tag != EXPR_IDENTIFIER || entity->identifier.keyword) return false;
    csview name = entity->identifier.name.sv;
    // `exports.x =`, `module.exports =`, `module.exports.x =` are CommonJS exports (or nothing outside a CommonJS
    // module: tsc's getAssignmentDeclarationKind)
    if (csview_equals(name, "exports")) return false;
    if (csview_equals(name, "module") && second && span_equals(expr_static_property_name(second), "exports")) return false;
    Symbol const* found = lookup_name_for_property_access(me, name);
    if (!found) return true;
    if (found->is_placeholder || (found->flags & SYMBOL_ALIAS)) return false;
    for (u32 i = 0; i < found->decl_count; i++) {
        if (found->decls[i].full.source == my file) return false;
    }
    return true;
}

// `exports` or `module` at the root of an entity name
static bool is_commonjs_root(Expr const* entity) {
    while (entity->tag == EXPR_PROPERTY) entity = &entity->property->expr;
    return entity->tag == EXPR_IDENTIFIER && !entity->identifier.keyword &&
        (span_equals(entity->identifier.name, "exports") || span_equals(entity->identifier.name, "module"));
}

// A global with expandos, whose variable initializers (in whichever file) get the members once every file is bound
static void push_global_expando(Binder* binder, Symbol* symbol) {
    if (binder->global_expando_count == binder->global_expando_cap) {
        binder->global_expando_cap = binder->global_expando_cap ? binder->global_expando_cap * 2 : 8;
        binder->global_expandos = realloc(binder->global_expandos, sizeof(Symbol*) * binder->global_expando_cap);
        panic_if(!binder->global_expandos, "out of memory");
    }
    binder->global_expandos[binder->global_expando_count++] = symbol;
}

// Each name of `a.b` of a top-level JavaScript `a.b.x = v` is a global namespace declared here, merged with the
// globals of that name (tsc's jsGlobalAugmentations, merged into the globals by initializeTypeChecker)
static Symbol* global_expando_namespace(Scope* me, Expr const* entity) {
    Symbol* parent = nullptr;
    SymbolTable* table = &my binder->globals;
    Span name;
    if (entity->tag == EXPR_PROPERTY) {
        parent = global_expando_namespace(me, &entity->property->expr);
        if (!parent) return nullptr;
        symbol_ensure_exports(parent);
        table = parent->exports;
        name = expr_static_property_name(entity);
    } else {
        name = entity->identifier.name;
    }
    bool merged;
    Symbol* symbol = declare_symbol(me, table, name.sv, name, SYMBOL_VALUE_MODULE, 0, false, &merged);
    if (!merged) {
        symbol->name = name.sv;
        symbol->name_span = name;
        symbol->parent = parent;
        symbol->exported = parent != nullptr;
    }
    if (symbol->flags & SYMBOL_ALIAS) return nullptr;
    symbol_ensure_exports(symbol);
    Span decl_name = entity->tag == EXPR_PROPERTY ? entity->property->prop.outer_span : entity->outer_span;
    symbol_add_decl(symbol, (SymbolDecl){
        .kind = SYMBOL_DECL_EXPANDO, .flags = SYMBOL_VALUE_MODULE, .node = entity, .name = decl_name, .full = decl_name,
    });
    push_global_expando(my binder, symbol);
    return symbol;
}

void binder_link_global_expandos(Binder* me) {
    for (u32 i = 0; i < my global_expando_count; i++) {
        Symbol* symbol = my global_expandos[i];
        for (u32 j = 0; j < symbol->decl_count; j++) {
            SymbolDecl const* decl = &symbol->decls[j];
            if (!decl->full.source || !decl->full.source->is_js) continue;
            Expr* init = nullptr;
            if (decl->kind == SYMBOL_DECL_VARIABLE) {
                Binding const* binding = decl->node;
                if (binding->parent || binding->tag != BINDING_IDENTIFIER) break;
                init = expando_initializer(&((InitializedBinding*)binding)->value, binding->content_span, true);
            } else if (decl->kind == SYMBOL_DECL_ASSIGNMENT) {
                init = expando_initializer((Expr*)decl->node, span_null, true);
            } else if (decl->kind == SYMBOL_DECL_EXPANDO) {
                continue;
            }
            set_expando_initializer_host(init, symbol);
            break;
        }
    }
}

// An assignment at the top level of a file to a property of `a.b` makes each of a and a.b a namespace: a declaration
// of it (tsc's bindPotentiallyMissingNamespaces), `full` null for one elsewhere, which is only a way into its members
static void add_expando_namespace_decls(Scope* me, Expr const* entity, bool top_level) {
    if (entity->tag == EXPR_PROPERTY) add_expando_namespace_decls(me, &entity->property->expr, top_level);
    Symbol* symbol = lookup_for_property_access(me, entity);
    if (!symbol && top_level && entity->tag == EXPR_PROPERTY) {
        // A name the namespace doesn't have yet is declared in it (tsc's forEachIdentifierInEntityName action)
        Symbol* parent = lookup_for_property_access(me, &entity->property->expr);
        Span name = expr_static_property_name(entity);
        if (parent && parent->exports && !parent->is_module && !(parent->flags & SYMBOL_ALIAS) && span_valid(name)) {
            bool merged;
            symbol = declare_symbol(me, parent->exports, name.sv, name, SYMBOL_VALUE_MODULE, 0, false, &merged);
            if (!merged) {
                symbol->name = name.sv;
                symbol->name_span = name;
                symbol->parent = parent;
                symbol->exported = true;
            }
        }
    }
    if (!symbol) return;
    Span name = entity->tag == EXPR_PROPERTY ? entity->property->prop.outer_span : entity->outer_span;
    symbol_ensure_exports(symbol);
    symbol->flags |= SYMBOL_VALUE_MODULE;
    bool has_expando = false;
    for (u32 i = 0; i < symbol->decl_count && !has_expando; i++) has_expando = symbol->decls[i].kind == SYMBOL_DECL_EXPANDO;
    if (top_level || !has_expando) {
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_EXPANDO, .flags = SYMBOL_VALUE_MODULE, .node = entity,
            .name = top_level ? name : span_null, .full = top_level ? name : span_null,
        });
    }
}

static void declare_assignment_member(
    Scope* me, SymbolTable** table, Symbol* parent, Span name, Expr* left, Expr* value, TypeNode const* jsdoc_type,
    AssignmentDeclKind kind, uint8_t jsdoc_modifiers
);

// The export that the entity name `exports.a.b` (`module.exports.a.b`, or through an alias of them) of a CommonJS module
// names, null if it isn't one or names nothing
static Symbol* commonjs_export_entity(Scope* module, Expr const* entity) {
    if (entity->tag != EXPR_PROPERTY || expr_is_perenthisized(entity)) return nullptr;
    Span name = expr_static_property_name(entity);
    if (!span_valid(name)) return nullptr;
    Expr const* object = &entity->property->expr;
    Symbol* parent = is_exports_alias(module, object) ? module->container : commonjs_export_entity(module, object);
    return parent && parent->exports ? symbol_table_get(parent->exports, name.sv) : nullptr;
}

// Each export of the entity name of a top-level `module.exports.a.b.x = v` gets a namespace declaration, like
// add_expando_namespace_decls() (the root is the file's symbol, which doesn't)
static void add_commonjs_export_namespace_decls(Scope* module, Expr const* entity) {
    Symbol* symbol = commonjs_export_entity(module, entity);
    if (!symbol) return;
    add_commonjs_export_namespace_decls(module, &entity->property->expr);
    symbol_ensure_exports(symbol);
    symbol->flags |= SYMBOL_VALUE_MODULE;
    Span name = entity->property->prop.outer_span;
    symbol_add_decl(symbol, (SymbolDecl){
        .kind = SYMBOL_DECL_EXPANDO, .flags = SYMBOL_VALUE_MODULE, .node = entity, .name = name, .full = name,
    });
}

// The name an assignment declaration `o.x = v` / `o["x"] = v` is written with: a string element access is named with
// its quotes (tsc's getNameOfSymbolAsWritten takes the declaration's name), which the symbols baseline prints as `o["x"]`
static Span written_member_name(Expr const* left, Span name) {
    if (left->tag == EXPR_PROPERTY && left->property->form == PROPERTY_EXPR_BRACKET &&
        left->property->prop.tag == EXPR_LITERAL && left->property->prop.literal.tag == VALUE_STRING
    ) {
        return left->property->prop.inner_span;
    }
    return name;
}

// JavaScript `F.prototype.x = v` declares an instance member x of F, which makes F a class (tsc's
// bindPrototypePropertyAssignment)
static void add_prototype_property(
    Scope* me, Expr* constructor, Span name, Expr* left, Expr* value, TypeNode const* jsdoc_type, AssignmentDeclKind kind
) {
    SourceFile const* file = my file;
    if (!file || !file->is_js) return;
    Expr* init;
    Symbol* host = expando_host(me, constructor, &init);
    if (!host) {
        // Any variable of this file gets the member (tsc's bindPotentiallyMissingNamespaces makes whatever has a value
        // declaration a class: \`var g = f; g.prototype.m = v\`). Its type stays its initializer's, so it isn't made a
        // class here
        Symbol* found = lookup_for_property_access(me, constructor);
        if (!found || found->is_placeholder || !(found->flags & SYMBOL_VARIABLE) || (found->flags & (SYMBOL_ALIAS | SYMBOL_CLASS))) return;
        SymbolDecl const* variable = symbol_find_decl(found, SYMBOL_DECL_VARIABLE);
        if (!variable || variable->full.source != file) return;
        declare_assignment_member(me, &found->members, found, name, left, value, jsdoc_type, ASSIGNMENT_DECL_PROTOTYPE_PROPERTY, 0);
        return;
    }
    if (init && (init->tag != EXPR_FUNCTION || span_valid(init->function->arrow_span))) return;
    if (!init && !(host->flags & (SYMBOL_FUNCTION | SYMBOL_CLASS))) return;
    if (init) init->function->class_symbol = host;
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    host->flags |= SYMBOL_CLASS;
    declare_assignment_member(me, &host->members, host, name, left, value, jsdoc_type, kind, 0);
}

static Symbol* js_constructor_symbol(Symbol* symbol);

// `f.x = v` (tsc's bindStaticPropertyAssignment -> bindPropertyAssignment for AssignmentDeclarationKind.Property)
static void add_expando_decl(Scope* me, Expr* left, Expr* right, TypeNode const* jsdoc_type, bool top_level);
static void add_expando_member(
    Scope* me, Expr* object, Span name, Expr* left, Expr* right, TypeNode const* jsdoc_type, bool top_level,
    Expr const* late_bound_key, AssignmentDeclKind kind
);

static void add_expando(Scope* me, BinaryOpExpr* op, bool top_level) {
    if (op->op != BINARY_OP_EQUAL) return;
    op->expando_bound = true;
    add_expando_decl(me, &op->left, &op->right, op->jsdoc_type, top_level);
}

// `left = right`, or a JavaScript `/** @type {T} */ left;` (right is left then)
static void add_expando_decl(Scope* me, Expr* left, Expr* right, TypeNode const* jsdoc_type, bool top_level) {
    if (left->tag != EXPR_PROPERTY) return;
    Span name = expr_static_property_name(left);
    Expr* object = &left->property->expr;
    // `f[k] = v` with an entity name k: a late bound member of a function or class (tsc's
    // isBindableStaticElementAccessExpression, addLateBoundAssignmentDeclarationToSymbol). Named `[k]` as written
    Expr const* late_bound_key = nullptr;
    if (!span_valid(name) && left->property->form == PROPERTY_EXPR_BRACKET && !left->property->optional_chain &&
        !expr_is_perenthisized(left) && expr_is_entity_name(&left->property->prop)
    ) {
        late_bound_key = &left->property->prop;
        name = span_make(left->outer_span.source, span_end_ptr(object->outer_span), span_end_ptr(left->outer_span));
        if (!span_valid(name) || !csview_starts_with(name.sv, "[")) return;
    }
    if (!span_valid(name)) return;
    if (late_bound_key && (expr_prototype_object(object) || !expr_is_entity_name(object))) return;
    add_expando_member(me, object, name, left, right, jsdoc_type, top_level, late_bound_key, ASSIGNMENT_DECL_PROPERTY);
}

// `Object.defineProperty(o, "x", {...})` of a container o (tsc's bindObjectDefinePropertyAssignment) or of
// `F.prototype` (bindObjectDefinePrototypeProperty): declared by the call
static void add_define_property(Scope* me, Expr* call, bool top_level) {
    Span name;
    if (!expr_is_define_property_call(call, &name)) return;
    Expr* object = (Expr*)vec_expr_at(&call->call->args, 0);
    if (expr_is_exports_object(object)) return; // (add_define_property_export())
    add_expando_member(me, object, name, call, call, nullptr, top_level, nullptr, ASSIGNMENT_DECL_DEFINE_PROPERTY);
}

// Member `name` of what `object` names, declared by `left` (`object.name`, or a call) = `right`
static void add_expando_member(
    Scope* me, Expr* object, Span name, Expr* left, Expr* right, TypeNode const* jsdoc_type, bool top_level,
    Expr const* late_bound_key, AssignmentDeclKind kind
) {
    Expr* prototype = expr_prototype_object(object);
    if (prototype) {
        add_prototype_property(me, prototype, name, left, right, jsdoc_type,
            kind == ASSIGNMENT_DECL_PROPERTY ? ASSIGNMENT_DECL_PROTOTYPE_PROPERTY : kind);
        return;
    }
    // A private name is no expando (tsc's getAssignmentDeclarationPropertyAccessKind: isBindableStaticNameExpression
    // of the target, only a prototype property doesn't ask)
    if (name.sv.size && name.sv.buf[0] == '#') return;
    // `F.prototype = { ... }` declares prototype members in JavaScript and nothing elsewhere (tsc's
    // getAssignmentDeclarationKind: only the Property kind outside JavaScript)
    if (span_equals(name, "prototype") && right->tag == EXPR_OBJECT && !(my file && my file->is_js)) return;
    // `a.x = v` where a is `module.exports` is an export (add_commonjs_export())
    Scope* module = my file && my file->is_commonjs ? commonjs_module_scope(me) : nullptr;
    if (module && is_exports_alias(module, object)) return;
    Expr* init;
    Symbol* host = expando_host(me, object, &init);
    Symbol* exported = top_level && module ? commonjs_export_entity(module, object) : nullptr;
    if (exported) {
        // `module.exports.f.x = v` at the top level: f is the export (tsc's forEachIdentifierInEntityName starts from
        // the file's symbol), it becomes a namespace like any entity name there
        host = symbol_is_expando_host(exported, my file, true, &init) ? exported : nullptr;
        if (!host) return;
        set_expando_initializer_host(init, host);
        add_commonjs_export_namespace_decls(module, object);
    } else if (host) {
        set_expando_initializer_host(init, host);
        // A global merged across files (tsc's mergeSymbol with the Assignment flag): the variable of another file that
        // it may take its value from gets the members too
        if (!init && object->tag == EXPR_IDENTIFIER && my file->is_js && symbol_table_get(&my binder->globals, host->name) == host) {
            push_global_expando(my binder, host);
        }
        add_expando_namespace_decls(me, object, top_level);
    } else if (late_bound_key) {
        return;
    } else if (top_level && my file->is_js && is_global_expando_entity(me, left)) {
        host = global_expando_namespace(me, object);
        if (!host) return;
    } else {
        // What the file declares that can't have expandos still gets the namespace declarations (tsc's
        // bindPotentiallyMissingNamespaces), which only the symbols baseline shows
        Expr const* root = object;
        bool private_name = false;
        while (root->tag == EXPR_PROPERTY) {
            private_name |= root->property->prop.inner_span.sv.size && root->property->prop.inner_span.sv.buf[0] == '#';
            root = &root->property->expr;
        }
        Symbol const* root_symbol = root->tag == EXPR_IDENTIFIER ? lookup_for_property_access(me, root) : nullptr;
        if (top_level && my file->is_js && expr_is_entity_name(object) && name.sv.buf[0] != '#' && !private_name &&
            root_symbol && !(root_symbol->flags & SYMBOL_ALIAS) && !root_symbol->is_placeholder &&
            !(my file->is_commonjs && is_commonjs_root(object))
        ) {
            add_expando_namespace_decls(me, object, true);
            // Which makes each name of it a namespace, an expando host (tsc's bindPotentiallyMissingNamespaces, then
            // isExpandoSymbol: SymbolFlags.NamespaceModule), whatever the other files that declare it say
            host = lookup_for_property_access(me, object);
            // (Declared in another file: its class expression or function initializer there gets the members)
            for (u32 i = 0; host && i < host->decl_count; i++) {
                SourceFile const* other = host->decls[i].full.source;
                Expr* other_init;
                if (other && other != my file && symbol_is_expando_host(host, other, other->is_js, &other_init)) {
                    set_expando_initializer_host(other_init, host);
                }
            }
        }
        if (!host) return;
    }
    // The member (bindPotentiallyNewExpandoMemberToNamespace)
    bool merged;
    Symbol* member = declare_symbol(me, host->exports, name.sv, name, SYMBOL_PROPERTY, 0, false, &merged);
    if (!merged) {
        member->name = name.sv;
        member->name_span = written_member_name(left, name);
        member->parent = host;
        member->exported = true;
    }
    Expr* value = right;
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    if (late_bound_key && !member->late_bound_key) member->late_bound_key = late_bound_key;
    if (left->tag == EXPR_PROPERTY) left->property->declares = member;
    if (left->tag == EXPR_CALL) left->call->declares = member;
    symbol_add_decl(member, (SymbolDecl){
        .kind = SYMBOL_DECL_ASSIGNMENT, .assignment = kind, .flags = SYMBOL_PROPERTY, .node = value,
        .name = left->outer_span, .full = left->outer_span, .jsdoc_type = jsdoc_type,
    });
    set_function_class_symbol(value, member);
    // JavaScript `F.prototype = { ... }` makes F a class (tsc's bindPrototypeAssignment: containerIsClass)
    if (span_equals(name, "prototype") && value->tag == EXPR_OBJECT && my file->is_js && js_constructor_symbol(host) &&
        !symbol_find_decl(host, SYMBOL_DECL_CLASS)
    ) {
        host->flags |= SYMBOL_CLASS;
        if (!host->members) host->members = arena_new(&my binder->arena, SymbolTable);
        if (init && init->tag == EXPR_FUNCTION) init->function->class_symbol = host;
    }
}

// Every assignment of a chain (`f.a = f.b = 1`)
static void add_expando_chain(Scope* me, Expr* e, bool top_level) {
    for (; e->tag == EXPR_BINARY_OP && e->binary_op->op == BINARY_OP_EQUAL; e = &e->binary_op->right) {
        if (expr_is_perenthisized(e)) break;
        add_expando(me, e->binary_op, top_level);
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
    add_expando(me, op, false);
}

// Whether a JavaScript scope's `this` is the global one: the top level of a script, blocks included (tsc's
// getThisContainer is the source file, through arrow functions)
// `arrow` is the innermost arrow function on the way, if any
static bool this_is_global_script(Scope* me, Scope** arrow) {
    *arrow = nullptr;
    if (!my file || !my file->is_js) return false;
    Scope* scope = me;
    while (scope && ((scope->kind == SCOPE_BLOCK && !scope->function) || (scope->function && span_valid(scope->function->arrow_span)))) {
        if (scope->kind == SCOPE_FUNCTION && !*arrow) *arrow = scope;
        scope = scope->parent;
    }
    return scope && scope->kind == SCOPE_SCRIPT;
}

// `this.x = v` at the top level of a JavaScript script declares a global variable x (tsc's bindThisPropertyAssignment
// for a SourceFile container: declareSymbolAndAddToSymbolTable as a FunctionScopedVariable). In an arrow function that
// adds it to the arrow function's locals (tsc's container), where `this.x` doesn't find it
static void add_global_this_chain(Scope* me, Expr* e, Scope* arrow) {
    for (; e->tag == EXPR_BINARY_OP && e->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(e); e = &e->binary_op->right) {
        Expr* left = &e->binary_op->left;
        Span name = expr_static_property_name(left);
        if (!span_valid(name) || name.sv.buf[0] == '#') continue;
        Expr const* object = &left->property->expr;
        if (object->tag != EXPR_IDENTIFIER || expr_is_perenthisized(object) || object->identifier.keyword != KEYWORD_IDENTIFIER_THIS) {
            continue;
        }
        Expr* value = &e->binary_op->right;
        while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
            value = &value->binary_op->right;
        }
        bool merged;
        Symbol* symbol = declare_symbol(
            me, arrow ? &arrow->locals : &my binder->globals, name.sv, name, SYMBOL_FUNCTION_SCOPED_VARIABLE,
            SYMBOL_FUNCTION_SCOPED_VARIABLE_EXCLUDES,
            false, &merged
        );
        if (!merged) {
            symbol->name = name.sv;
            symbol->name_span = written_member_name(left, name);
        }
        left->property->declares = symbol;
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_ASSIGNMENT, .flags = SYMBOL_FUNCTION_SCOPED_VARIABLE, .node = value, .name = left->outer_span,
            .full = left->outer_span, .jsdoc_type = e->binary_op->jsdoc_type,
            // (Not in a constructor: possibly undefined, tsc's getWidenedTypeForAssignmentDeclaration definedInMethod)
            .assignment = ASSIGNMENT_DECL_THIS_IN_METHOD,
        });
    }
}

void scope_add_expandos(Scope* me, Block const* block) {
    bool top_level = my kind == SCOPE_MODULE || my kind == SCOPE_SCRIPT;
    Scope* arrow;
    bool global_this = this_is_global_script(me, &arrow);
    for_each (i, vec_statement, &block->statements) {
        if (i->tag == STATEMENT_EXPR) add_expando_chain(me, &i->expr, top_level);
        if (i->tag == STATEMENT_EXPR && my file && my file->is_js) add_define_property(me, &i->expr, top_level);
        if (i->tag == STATEMENT_EXPR && global_this) add_global_this_chain(me, &i->expr, arrow);
        // `/** @type {T} */ a.b;` at the top level (tsc's bindSpecialPropertyDeclaration)
        if (i->tag == STATEMENT_EXPR && i->expr_jsdoc_type && top_level) {
            add_expando_decl(me, &i->expr, &i->expr, i->expr_jsdoc_type, top_level);
        }
        // `var a = f.x = {}` (not at the top level: the assignment's parent is the declaration)
        if (i->tag == STATEMENT_DECL_VARIABLE) {
            for_each (j, vec_initialized_binding, &i->decl.variable.initializers) add_expando_chain(me, &j->value, false);
        }
    }
}

// JavaScript classes

// The symbol of the class a constructor function (or the constructor of `X.prototype.m = function`) makes: a function
// declaration, or a variable initialized with a function expression. Null if `symbol` is neither
static Symbol* js_constructor_symbol(Symbol* symbol) {
    if (!symbol || symbol->is_placeholder || (symbol->flags & SYMBOL_ALIAS)) return nullptr;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->kind == SYMBOL_DECL_FUNCTION) return symbol;
        if (decl->kind == SYMBOL_DECL_CLASS) return symbol;
        if (decl->kind == SYMBOL_DECL_VARIABLE) {
            Binding const* binding = decl->node;
            if (binding->parent || binding->tag != BINDING_IDENTIFIER) return nullptr;
            Expr const* init = &((InitializedBinding const*)binding)->value;
            return init->tag == EXPR_FUNCTION && !span_valid(init->function->arrow_span) ? symbol : nullptr;
        }
        // `ns.F = function () {}`
        if (decl->kind == SYMBOL_DECL_ASSIGNMENT) return symbol_decl_js_function(decl) ? symbol : nullptr;
    }
    return nullptr;
}

// Declares an assignment member `name` (`left` = v) in a class's or constructor function's table
static void declare_assignment_member(
    Scope* me, SymbolTable** table, Symbol* parent, Span name, Expr* left, Expr* value, TypeNode const* jsdoc_type,
    AssignmentDeclKind kind, uint8_t jsdoc_modifiers
) {
    if (!*table) *table = arena_new(&my binder->arena, SymbolTable);
    bool merged;
    Symbol* member = declare_symbol(me, *table, name.sv, name, SYMBOL_PROPERTY, 0, false, &merged);
    if (!merged) {
        member->name = name.sv;
        member->name_span = written_member_name(left, name);
        member->parent = parent;
        member->exported = true;
    }
    if (left->tag == EXPR_PROPERTY) left->property->declares = member;
    if (left->tag == EXPR_CALL) left->call->declares = member;
    symbol_add_decl(member, (SymbolDecl){
        .kind = SYMBOL_DECL_ASSIGNMENT, .assignment = kind, .flags = SYMBOL_PROPERTY, .node = value,
        .name = left->outer_span, .full = left->outer_span, .jsdoc_type = jsdoc_type, .jsdoc_modifiers = jsdoc_modifiers,
    });
    // In source order, like tsc's one binder walk (`F.prototype.x = v` is declared before the walk reaches a
    // `this.x = v` above it)
    for (u32 i = member->decl_count - 1; i > 0; i--) {
        SymbolDecl* a = &member->decls[i - 1];
        SymbolDecl* b = &member->decls[i];
        if (a->full.source != b->full.source || span_start_ptr(a->full) <= span_start_ptr(b->full)) break;
        SymbolDecl swap = *a;
        *a = *b;
        *b = swap;
    }
}

void scope_note_ts_this_assignment(Scope const* me, BinaryOpExpr const* op) {
    Expr const* left = &op->left;
    if (left->tag != EXPR_PROPERTY) return;
    Span name = expr_static_property_name(left);
    Expr const* prop = &left->property->prop;
    if (!span_valid(name) && prop->tag == EXPR_LITERAL && prop->literal.tag == VALUE_NUMBER) name = prop->inner_span; // `this[0]`
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
    ClassItem* field = span_valid(name) ? class_decl_find_member(class_decl, name.sv, is_static) : nullptr;
    // `this[k] = v` of a field `[k]: T`, written the same way
    for_each (i, vec_class_item, &class_decl->items) {
        if (field) break;
        if (i->tag == CLASS_ITEM_FIELD && i->field.computed_key && i->field.is_static == is_static &&
            csview_equals_sv(i->field.computed_key->outer_span.sv, prop->outer_span.sv)
        ) {
            field = (ClassItem*)i;
        }
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

void scope_add_this_declaration(Scope* me, Expr* access, TypeNode const* jsdoc_type) {
    if (!my file || !my file->is_js) return;
    // (An element access `/** @type {T} */ this["x"];` declares x, but the tag isn't its: tsc's
    // getNextJSDocCommentLocation only goes from a property access to its statement)
    if (access->tag == EXPR_PROPERTY && access->property->form == PROPERTY_EXPR_BRACKET) jsdoc_type = nullptr;
    // (The access is the declaration's node: its type is the JSDoc type)
    add_this_property(me, access, access, jsdoc_type, 0);
}

static void add_this_property(Scope* me, Expr* left, Expr* value, TypeNode const* jsdoc_type, uint8_t jsdoc_modifiers) {
    if (left->tag != EXPR_PROPERTY) return;
    Span name = expr_static_property_name(left);
    Expr const* object = &left->property->expr;
    // `this[k]` with an entity name k: a late bound name (tsc's isBindableStaticElementAccessExpression), a member of a
    // class only (tsc's bindThisPropertyAssignment: addLateBoundAssignmentDeclarationToSymbol). Named `[k]` as written
    Expr const* late_bound_key = nullptr;
    if (!span_valid(name) && left->property->form == PROPERTY_EXPR_BRACKET && !left->property->optional_chain &&
        !expr_is_perenthisized(left) && expr_is_entity_name(&left->property->prop)
    ) {
        late_bound_key = &left->property->prop;
        name = span_make(left->outer_span.source, span_end_ptr(object->outer_span), span_end_ptr(left->outer_span));
        if (!span_valid(name) || !csview_starts_with(name.sv, "[")) return;
    }
    // (Private names must be declared, even in JavaScript)
    if (!span_valid(name) || csview_starts_with(name.sv, "#")) return;
    // `this.x`, `var self = this; self.x` (tsc's isThisInitializedDeclaration)
    IdentifierExpr const* this = expr_this_or_alias(object);
    if (!this) return;
    if (late_bound_key && this->this_kind != THIS_CLASS_INSTANCE) return;
    // The function `this` belongs to (arrow functions have their container's)
    FunctionImpl const* function = nullptr;
    for (Scope const* scope = me; scope && !function; scope = scope->parent) {
        if (scope->kind == SCOPE_FUNCTION && scope->function && !span_valid(scope->function->arrow_span)) function = scope->function;
        if (scope->kind == SCOPE_STATIC_BLOCK || scope->kind == SCOPE_MODULE || scope->kind == SCOPE_SCRIPT) break;
    }
    switch (this->this_kind) {
        case THIS_CLASS_INSTANCE: {
            Symbol* class_symbol = (Symbol*)this->target;
            if (!class_symbol) return;
            AssignmentDeclKind kind = function && function->is_constructor ?
                ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR : ASSIGNMENT_DECL_THIS_IN_METHOD;
            declare_assignment_member(me, &class_symbol->members, class_symbol, name, left, value, jsdoc_type, kind, jsdoc_modifiers);
            Symbol* member = (Symbol*)symbol_table_get(class_symbol->members, name.sv);
            if (late_bound_key && member && !member->late_bound_key) member->late_bound_key = late_bound_key;
            if (late_bound_key) break;
            if (kind == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR && member && !member->constructor_assigned) {
                member->constructor_assigned = left;
                member->declaring_constructor = function;
            }
            // A field the class body declares without a type or initializer has the type the constructor assigns
            ClassDecl const* class_decl = symbol_find_node(class_symbol, SYMBOL_DECL_CLASS);
            ClassItem* field = class_decl ? class_decl_find_member(class_decl, name.sv, false) : nullptr;
            if (kind == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR && field && field->tag == CLASS_ITEM_FIELD &&
                !field->field.has_explicit_type && field->field.value.tag == EXPR_IMPLICIT_ANY
            ) {
                field->field.js_assigned = symbol_table_get(class_symbol->members, name.sv);
            }
        }   break;
        case THIS_CLASS_STATIC: {
            Symbol* class_symbol = (Symbol*)this->target;
            if (!class_symbol) return;
            symbol_ensure_exports(class_symbol);
            bool has_expando = false;
            for (u32 i = 0; i < class_symbol->decl_count; i++) has_expando |= class_symbol->decls[i].kind == SYMBOL_DECL_EXPANDO;
            if (!has_expando) {
                class_symbol->flags |= SYMBOL_VALUE_MODULE;
                symbol_add_decl(class_symbol, (SymbolDecl){ .kind = SYMBOL_DECL_EXPANDO, .flags = SYMBOL_VALUE_MODULE });
            }
            declare_assignment_member(me, &class_symbol->exports, class_symbol, name, left, value, jsdoc_type, ASSIGNMENT_DECL_THIS_IN_METHOD, jsdoc_modifiers);
        }   break;
        case THIS_PARAMETER: // (Of a JSDoc `@this {T}`: tsc's binder doesn't look at it)
            if (!this->this_function) break;
            // fallthrough
        case THIS_ANY: {
            FunctionImpl* this_function = (FunctionImpl*)this->this_function;
            if (!this_function || this_function != function) return;
            // `X.prototype.m = function () { this.x = v }` declares x for X, else the function is a constructor
            Symbol* class_symbol = nullptr;
            AssignmentDeclKind kind = ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR;
            Expr* assigned_to = (Expr*)this_function->this_assigned_object;
            Expr const* constructor = assigned_to ? expr_prototype_object(assigned_to) : nullptr;
            // (Not through an import: tsc's lookupSymbolForPropertyAccess finds the alias, which has no valueDeclaration)
            Expr const* root = constructor;
            while (root && root->tag == EXPR_PROPERTY) root = &root->property->expr;
            if (root && root->tag == EXPR_IDENTIFIER && root->identifier.symbol && (root->identifier.symbol->flags & SYMBOL_ALIAS)) {
                return;
            }
            if (constructor) {
                class_symbol = js_constructor_symbol((Symbol*)expr_entity_name_symbol(constructor));
                kind = ASSIGNMENT_DECL_THIS_IN_METHOD;
            } else if (this_function->prototype_of && this_function->this_literal) {
                // A method of `F.prototype = { m() { this.x = v } }`: tsc declares x in the literal's members, which
                // are F's instance members
                class_symbol = js_constructor_symbol((Symbol*)expr_entity_name_symbol(this_function->prototype_of));
                kind = ASSIGNMENT_DECL_THIS_IN_METHOD;
            } else if (this_function->class_symbol) {
                class_symbol = this_function->class_symbol; // `ns.C = function () {}` with `ns.C.prototype.m = v`
            } else if (!assigned_to) {
                class_symbol = this_function->class_symbol ? this_function->class_symbol :
                    this_function->is_expression ? nullptr : this_function->symbol;
            }
            if (!class_symbol) return;
            class_symbol->flags |= SYMBOL_CLASS;
            Symbol* existing = class_symbol->members ? (Symbol*)symbol_table_get(class_symbol->members, name.sv) : nullptr;
            declare_assignment_member(me, &class_symbol->members, class_symbol, name, left, value, jsdoc_type, kind, jsdoc_modifiers);
            // (Its type is what `this.x` is at the end of the constructor function: tsc's getFlowTypeInConstructor)
            Symbol* member = (Symbol*)symbol_table_get(class_symbol->members, name.sv);
            bool literal_method = !constructor && this_function->prototype_of && this_function->this_literal;
            if (member) member->prototype_literal_member = literal_method && (!existing || existing->prototype_literal_member);
            if (kind == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR && member && !member->constructor_assigned && function == this_function) {
                member->constructor_assigned = left;
                member->declaring_constructor = function;
            }
        }   break;
        default: break;
    }
}
