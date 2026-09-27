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

void binder_destroy(Binder* me) {
    if (!me) return;
    free(my symbols);
    free(my pattern_modules);
    free(my augmentations);
    free(my pending_not_found);
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
    if (kind == SCOPE_BLOCK) ret->function = parent->function;
    return ret;
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
        if (await && my file && my file->is_module && !my function) {
            error_ctx_error_d(my errs, name, 1262, "Identifier expected. '", name, "' is a reserved word at the top-level of a module.");
        } else if ((await && my function && my function->is_async) || (yield && my function && my function->is_generator)) {
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

Scope* scope_new_generic(Scope* parent, Generic* generic, ErrorCtx* errs) {
    if (generic_empty(generic)) return parent;
    Scope* ret = scope_new(parent, SCOPE_BLOCK);
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

Scope* scope_new_generic_merged(Scope* parent, Generic* generic, Symbol const* owner, void const* node, ErrorCtx* errs) {
    if (generic_empty(generic)) return parent;
    Scope* ret = scope_new(parent, SCOPE_BLOCK);
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
    switch (code) {
        case 2567:
            return error_ctx_error_d(errs, at, 2567,
                "Enum declarations can only merge with namespace or other enum declarations.");
        case 2451:
            return error_ctx_error_d(errs, at, 2451, "Cannot redeclare block-scoped variable '", at, "'.");
        default:
            return error_ctx_error_d(errs, at, 2300, "Duplicate identifier '", at, "'.");
    }
}

// tsc's addDuplicateDeclarationError: the checker's merges (across files) point each error at the other side's
// declarations, the binder's (within a file) don't
static void add_duplicate_related(Error* error, Span at, Span other) {
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
    for (u32 i = 0; i < existing->decl_count; i++) {
        Error* error = report_conflict(errs, existing->decls[i].name, code);
        if (related) add_duplicate_related(error, existing->decls[i].name, name);
    }
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

// tsc's checkTypeNameIsReserved: the predefined type keywords can't name a type
static void check_type_name_is_reserved(ErrorCtx* errs, Span name, SymbolDeclKind kind) {
    static char const* const reserved[] = {
        "any", "unknown", "never", "number", "bigint", "boolean", "string", "symbol", "void", "object", "undefined",
    };
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
    if (!span_valid(name) || !name.source || name.source->is_stdlib) return;
    for (u32 i = 0; i < c_arraylen(reserved); i++) {
        if (!csview_equals(name.sv, reserved[i])) continue;
        error_ctx_error_d(errs, name, code, what, " name cannot be '", name, "'.");
        return;
    }
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
        if (existing->flags & excludes) {
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
    if (props.kind == SYMBOL_DECL_VARIABLE || props.kind == SYMBOL_DECL_CLASS || props.kind == SYMBOL_DECL_ENUM) {
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

void scope_check_duplicate_exports(Scope* me) {
    if (!my container || !my container->exports) return;
    SymbolTable const* exports = my container->exports;
    Symbol const* export_equals = symbol_table_get(exports, c_sv("export="));
    if (export_equals && exports->count > 1 && export_equals->decl_count) {
        // (tsc's checkExternalModuleExports)
        error_ctx_error_d(my errs, export_equals->decls[0].full, 2309,
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
            error_ctx_error_d(my errs, symbol_decl_error_span(decl), 2323, "Cannot redeclare exported variable '", exports->entries[i].name,
                "'.");
        }
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
            // A global alias of the module (UMD). TODO: TS2686 when a module uses it
            if (my kind != SCOPE_MODULE || !my container) return;
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

void scope_export_default_expr(Scope* me, Expr* expr, Span statement, bool export_equals) {
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
    Span name = alias ? expr->outer_span : statement;
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
        });
    } else {
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_EXPR, .flags = SYMBOL_BLOCK_SCOPED_VARIABLE, .node = expr,
            .name = statement, .full = statement,
        });
    }
}

// CommonJS

// `exports.x = v`: an alias when v is a name (tsc's isAliasableExpression), else a property whose type is what is
// assigned (several assignments merge)
static void declare_commonjs_export(Scope* me, csview name, Span name_span, Expr* value, Span left, Span statement) {
    symbol_ensure_exports(my container);
    bool alias = value->tag == EXPR_IDENTIFIER && !value->identifier.keyword;
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
        symbol->alias->name = value->identifier.name;
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_ALIAS, .flags = SYMBOL_ALIAS, .name = left, .full = statement,
        });
    } else {
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_ASSIGNMENT, .flags = SYMBOL_PROPERTY, .node = value, .name = left, .full = statement,
        });
    }
}

// tsc's getRightMostAssignedExpression: `module.exports = exports = x` assigns x
static Expr* rightmost_assigned(Expr* value) {
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    return value;
}

static void add_commonjs_export(Scope* me, BinaryOpExpr* op, Span statement);

void scope_add_commonjs_export(Scope* me, Expr* assignment, Span statement) {
    if (my kind != SCOPE_MODULE || !my file || !my file->is_commonjs || !my container) return;
    // Every assignment of a chain (`exports = module.exports = C`)
    while (assignment->tag == EXPR_BINARY_OP && assignment->binary_op->op == BINARY_OP_EQUAL) {
        add_commonjs_export(me, assignment->binary_op, statement);
        assignment = &assignment->binary_op->right;
    }
}

static void add_commonjs_export(Scope* me, BinaryOpExpr* op, Span statement) {
    Span name = span_null;
    switch (binary_commonjs_declaration(op, &name)) {
        case COMMONJS_TARGET_NONE:
            break;
        case COMMONJS_TARGET_EXPORTS_PROPERTY:
            declare_commonjs_export(me, name.sv, name, &op->right, op->left.outer_span, statement);
            break;
        case COMMONJS_TARGET_MODULE_EXPORTS: {
            Expr* value = rightmost_assigned(&op->right);
            if (value->tag == EXPR_OBJECT) {
                // `module.exports = {}` declares nothing, `{ a, b }` its shorthands (tsc's
                // bindExportAssignedObjectMemberAlias)
                bool shorthands = true;
                for_each (i, vec_object_expr_item, &value->object) shorthands &= i->tag == OBJECT_ITEM_SHORTHAND;
                if (shorthands) {
                    for_each (i, vec_object_expr_item, &value->object) {
                        declare_commonjs_export(me, i->name_span.sv, i->name_span, &i->value, i->name_span, i->span);
                    }
                    break;
                }
            }
            declare_commonjs_export(me, c_sv("export="), op->left.outer_span, value, op->left.outer_span, statement);
        }   break;
    }
}

void scope_add_commonjs_module(Scope* me) {
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

Symbol* module_export_equals(Symbol* module) {
    if (!module || !module->is_module) return module;
    Symbol* assigned = symbol_table_get(module->exports, c_sv("export="));
    if (!assigned) return module;
    Symbol* target = assigned->flags & SYMBOL_ALIAS ? symbol_resolve_alias(assigned) : assigned;
    return target ? target : module;
}

static bool module_visited(Symbol** visited, u32 count, Symbol const* module) {
    for (u32 i = 0; i < count; i++) {
        if (visited[i] == module) return true;
    }
    return false;
}

// tsc's extendExportStars(): own exports shadow what comes through `export *`, of two stars the first wins, `default`
// doesn't go through a star
static void extend_export_stars(
    SymbolTable* target, Arena* arena, Symbol* module, Symbol*** visited, u32* visited_count, u32* visited_cap,
    bool nested
) {
    if (*visited_count == *visited_cap) {
        *visited_cap = *visited_cap ? *visited_cap * 2 : 16;
        *visited = realloc(*visited, sizeof(Symbol*) * *visited_cap);
    }
    (*visited)[(*visited_count)++] = module;

    u32 count;
    SymbolEntry* own = symbol_table_collect(module->exports, &count);
    for (u32 i = 0; i < count; i++) {
        if (nested && (csview_equals(own[i].name, "default") || csview_equals(own[i].name, "export="))) continue;
        if (!symbol_table_get_own(target, own[i].name, own[i].hash)) {
            symbol_table_insert(target, arena, own[i].name, own[i].hash, own[i].symbol);
        }
    }
    free(own);

    for (u32 i = 0; i < module->module->export_star_count; i++) {
        Symbol* next = binder_module_of_ref(module->binder, module->module->export_stars[i]->module_ref);
        if (!next || module_visited(*visited, *visited_count, next)) continue;
        extend_export_stars(target, arena, next, visited, visited_count, visited_cap, true);
    }
}

static SymbolTable const* module_resolved_exports(Symbol* me) {
    if (!my module->export_star_count) return my exports;
    if (my module->resolved_exports_state == ALIAS_DONE) return my module->resolved_exports;
    SymbolTable* table = arena_new(&my binder->arena, SymbolTable);
    Symbol** visited = nullptr;
    u32 visited_count = 0;
    u32 visited_cap = 0;
    extend_export_stars(table, &my binder->arena, me, &visited, &visited_count, &visited_cap, false);
    free(visited);
    my module->resolved_exports = table;
    my module->resolved_exports_state = ALIAS_DONE;
    return table;
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
    return me && symbol_is_pure_alias(me) ? symbol_resolve_alias(me) : me;
}

static Symbol* resolve_import_alias_path(Symbol* me) {
    PortItem* item = my alias->item;
    ErrorCtx* errs = my alias->scope->errs;
    if (vec_span_is_empty(&item->alias_path)) return nullptr;
    Span first = *vec_span_front(&item->alias_path);
    Symbol* target;
    if (vec_span_size(&item->alias_path) == 1) {
        target = scope_resolve(my alias->scope, first.sv, SYMBOL_NAMESPACE);
        if (!target) target = scope_resolve(my alias->scope, first.sv, SYMBOL_VALUE);
        if (!target) target = scope_resolve(my alias->scope, first.sv, SYMBOL_TYPE);
        if (target == me) target = nullptr;
        if (!target) {
            error_ctx_error_d(errs, first, 2304, "Cannot find name '", first, "'.");
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
        Span prev = *vec_span_at(&item->alias_path, i - 1);
        bool last = i == vec_span_size(&item->alias_path) - 1;
        Symbol const* container = symbol_for_meaning(target, SYMBOL_NAMESPACE);
        if (!container || container->is_placeholder) return nullptr; // A placeholder: anything goes
        Symbol* found = symbol_get_export(container, name.sv);
        Symbol* next = resolve_if_pure_alias(found);
        if (!next || (!last && !(symbol_resolved_flags(next) & SYMBOL_NAMESPACE) && !next->is_placeholder)) {
            error_ctx_error_d(errs, name, 2694, "Namespace '", prev, "' has no exported member '", name, "'.");
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

static Symbol* resolve_alias_target(Symbol* me) {
    PortItem* item = my alias->item;
    ErrorCtx* errs = my alias->scope->errs;
    if (!item) {
        // `export default x`, TS2304 comes from the expression
        Symbol* found = scope_resolve(my alias->scope, my alias->name.sv, SYMBOL_ANY_MEANING);
        if (found == me) return nullptr;
        unused_mark_referenced(my alias->scope, found, SYMBOL_ANY_MEANING);
        return resolve_if_pure_alias(found);
    }
    Symbol* module = nullptr;
    Symbol* assigned = nullptr; // What its `export =` names, if it has one
    if (item->tag != PORT_IMPORT_ALIAS && item->tag != PORT_EXPORT_SPECIFIC && item->tag != PORT_EXPORT_AS_NAMESPACE) {
        module = binder_module_of_ref(my binder, item->module_ref);
        if (!module) {
            // No file: TS2307 comes from the statement
            if (item->module_ref->file) {
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
                    if (!found) return property;
                    my alias->value_target = property;
                }
                if (!found) return nullptr;
            }
            if (!found) {
                cstr module_name = module_name_for_error(module, item->module_ref, true);
                error_ctx_error_d(errs, item->source_name, 2305,
                    "Module '", module_name, "' has no exported member '", item->source_name, "'.");
                cstr_drop(&module_name);
                return nullptr;
            }
            item->source_symbol = found;
            return resolve_if_pure_alias(found);
        }

        case PORT_IMPORT_DEFAULT: {
            Symbol* found = symbol_get_export(module, c_sv("default"));
            if (!found && assigned) {
                // esModuleInterop / allowSyntheticDefaultImports: the default import is what `export =` names.
                // TODO: TS1259 without them
                return assigned;
            }
            if (!found) {
                SourceFile const* file = item->module_ref->file;
                if (compiler_options()->allow_synthetic_default_imports && file && file->is_commonjs &&
                    !symbol_get_export(module, c_sv("__esModule"))
                ) {
                    // A CommonJS JavaScript module's synthetic default is the module (tsc's canHaveSyntheticDefault)
                    return module;
                }
                if (compiler_options()->allow_synthetic_default_imports && (!file || file->is_declaration)) {
                    return nullptr;
                }
                cstr module_name = module_name_for_error(module, item->module_ref, false);
                error_ctx_error_d(errs, item->decl_name, 1192, "Module '", module_name, "' has no default export.");
                cstr_drop(&module_name);
                return nullptr;
            }
            item->source_symbol = found;
            return resolve_if_pure_alias(found);
        }

        case PORT_EXPORT_SPECIFIC: {
            Symbol* found = scope_resolve(my alias->scope, item->source_name.sv, SYMBOL_ANY_MEANING);
            if (!found || found == me) {
                error_ctx_error_d(errs, item->source_name, 2304, "Cannot find name '", item->source_name, "'.");
                return nullptr;
            }
            unused_mark_referenced(my alias->scope, found, SYMBOL_ANY_MEANING);
            item->source_symbol = found;
            return resolve_if_pure_alias(found);
        }

        case PORT_IMPORT_GATHER:
        case PORT_REEXPORT_GATHER:
            if (assigned) return assigned;
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
    my alias->state = ALIAS_IN_FLIGHT;
    Symbol* target = resolve_alias_target(me);
    // (`export default x` looks its name up in a scope that has it)
    if (my alias->state == ALIAS_CIRCULAR && my alias->item) {
        Span at = my alias->item->tag == PORT_IMPORT_ALIAS ? my alias->item->full_span : my alias->item->decl_name;
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
        flags |= resolved_flags(symbol_resolve_alias((Symbol*)me), depth + 1);
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
            if (item->tag == PORT_IMPORT_ALIAS) at = item->full_span;
            if (item->tag == PORT_IMPORT_SPECIFIC && span_valid(item->source_name)) {
                at = span_between(item->source_name, item->decl_name);
            }
            error_ctx_error_d(errs, at, 2440,
                "Import declaration conflicts with local declaration of '", item->decl_name, "'.");
        }
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
    static char const* const primitives[] = { "string", "number", "boolean", "object", "bigint", "symbol" };
    u32 extra = globals ? c_arraylen(primitives) : 0;
    csview* names = malloc(sizeof(csview) * (count + extra + 1));
    panic_if(!names, "out of memory");
    for (u32 i = 0; i < extra; i++) {
        names[i] = (csview){0};
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
    bool from_parameters = false;
    bool arguments = (meaning & SYMBOL_VARIABLE) && csview_equals(name, "arguments");
    bool arrows_have_arguments = compiler_options()->target < SCRIPT_TARGET_ES2015;
    for (Scope const* scope = me; scope; scope = scope->parent) {
        SymbolTable const* table = scope->kind == SCOPE_GLOBAL ? &scope->binder->globals : &scope->locals;
        if (table->count || table->parent) {
            Symbol* found = symbol_table_lookup(table, name, hash);
            bool body_variable = from_parameters && found && (found->flags & SYMBOL_VARIABLE) && !found->is_parameter &&
                compiler_options()->target >= SCRIPT_TARGET_ES2015;
            if (found && !body_variable && symbol_has_meaning(found, meaning)) return found;
        }
        if (arguments && scope->kind == SCOPE_FUNCTION && scope->function &&
            (arrows_have_arguments || !span_valid(scope->function->arrow_span))
        ) {
            // A function's own `arguments` hides outer declarations (tsc's resolveNameHelper returns argumentsSymbol):
            // no symbol, see IdentifierExpr.is_arguments
            return nullptr;
        }
        from_parameters = scope->parameters && !scope->parameters_see_body;
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
            // `export { a }` must not find itself
            if (found && !is_export_specifier_alias(found) && symbol_has_meaning(found, meaning)) return found;
        }
    }
    return nullptr;
}

ClassItem const* scope_invalid_initializer_field(Scope const* me, csview name, Symbol const* found) {
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
        return generic_empty(generic) ? nullptr : generic;
    }
    return nullptr;
}

// Expandos

// The expression a variable of this initializer is an expando container of (tsc's getDeclaredExpandoInitializer):
// a function or arrow function, in JavaScript also a class expression or `{}`, possibly defaulted (`var ns = ns || {}`)
Expr* expando_initializer(Expr* init, Span name, bool js) {
    if (init->tag == EXPR_BINARY_OP && !expr_is_perenthisized(init) &&
        (init->binary_op->op == BINARY_OP_OR_OR || init->binary_op->op == BINARY_OP_EH_EH)
    ) {
        // `var ns = ns || {}`, `ns.a = ns.a || {}` (tsc's getDefaultedExpandoInitializer: the same name on the left)
        Expr const* left = &init->binary_op->left;
        bool same = span_valid(name) ? left->tag == EXPR_IDENTIFIER && csview_equals_sv(left->identifier.name.sv, name.sv) :
            left->tag == EXPR_PROPERTY || left->tag == EXPR_IDENTIFIER;
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

// What `f` of `f.x = v` names, if it can have expandos: tsc's isExpandoSymbol, and in TypeScript isFunctionSymbol.
// `init_ret`: a variable's expando initializer
static Symbol* expando_host(Scope* me, Expr const* entity, Expr** init_ret) {
    *init_ret = nullptr;
    SourceFile const* file = my file;
    bool js = file && file->is_js;
    Symbol* found = lookup_for_property_access(me, entity);
    if (!found || found->is_placeholder || (found->flags & SYMBOL_ALIAS)) return nullptr;
    // A global declared in another file isn't this file's to extend (tsc looks in the file's locals)
    bool here = false;
    for (u32 i = 0; i < found->decl_count && !here; i++) here = found->decls[i].full.source == file;
    if (!here) return nullptr;
    SymbolDecl const* value = nullptr;
    for (u32 i = 0; i < found->decl_count && !value; i++) {
        if (found->decls[i].flags & SYMBOL_VALUE) value = &found->decls[i];
    }
    if (!value) return nullptr;
    switch (value->kind) {
        case SYMBOL_DECL_FUNCTION: return found;
        case SYMBOL_DECL_CLASS:
        case SYMBOL_DECL_NAMESPACE:
        case SYMBOL_DECL_ENUM:
            return js ? found : nullptr;
        case SYMBOL_DECL_VARIABLE: {
            Binding const* binding = value->node;
            if (binding->parent || binding->tag != BINDING_IDENTIFIER) return nullptr;
            InitializedBinding* initialized = (InitializedBinding*)binding;
            Expr* init = expando_initializer(&initialized->value, binding->content_span, js);
            if (!init) return nullptr;
            // (An annotated variable has the type it says, but its initializer gets the members all the same)
            *init_ret = init;
            return found;
        }
        case SYMBOL_DECL_ASSIGNMENT: {
            // JavaScript `ns.a = {}` (or a function, a class): `ns.a.b = v` declares b in it
            if (!js) return nullptr;
            Expr* init = expando_initializer((Expr*)value->node, span_null, js);
            if (!init) return nullptr;
            *init_ret = init;
            return found;
        }
        default: return nullptr;
    }
}

// An assignment at the top level of a file to a property of `a.b` makes each of a and a.b a namespace: a declaration
// of it (tsc's bindPotentiallyMissingNamespaces), `full` null for one elsewhere, which is only a way into its members
static void add_expando_namespace_decls(Scope* me, Expr const* entity, bool top_level) {
    if (entity->tag == EXPR_PROPERTY) add_expando_namespace_decls(me, &entity->property->expr, top_level);
    Symbol* symbol = lookup_for_property_access(me, entity);
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
    Scope* me, SymbolTable** table, Symbol* parent, Span name, Expr* left, Expr* value, AssignmentDeclKind kind
);

// JavaScript `F.prototype.x = v` declares an instance member x of F, which makes F a class (tsc's
// bindPrototypePropertyAssignment)
static void add_prototype_property(Scope* me, Expr* constructor, Span name, Expr* left, Expr* value) {
    SourceFile const* file = my file;
    if (!file || !file->is_js) return;
    Expr* init;
    Symbol* host = expando_host(me, constructor, &init);
    if (!host) return;
    if (init && (init->tag != EXPR_FUNCTION || span_valid(init->function->arrow_span))) return;
    if (!init && !(host->flags & (SYMBOL_FUNCTION | SYMBOL_CLASS))) return;
    if (init) init->function->class_symbol = host;
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    host->flags |= SYMBOL_CLASS;
    declare_assignment_member(me, &host->members, host, name, left, value, ASSIGNMENT_DECL_PROTOTYPE_PROPERTY);
}

// `f.x = v` (tsc's bindStaticPropertyAssignment -> bindPropertyAssignment for AssignmentDeclarationKind.Property)
static void add_expando(Scope* me, BinaryOpExpr* op, bool top_level) {
    if (op->op != BINARY_OP_EQUAL) return;
    Expr* left = &op->left;
    Span name = expr_static_property_name(left);
    if (!span_valid(name)) return;
    Expr* object = &left->property->expr;
    Expr* prototype = expr_prototype_object(object);
    if (prototype) {
        add_prototype_property(me, prototype, name, left, &op->right);
        return;
    }
    Expr* init;
    Symbol* host = expando_host(me, object, &init);
    if (!host) return;
    if (init && init->tag == EXPR_FUNCTION) init->function->expando_host = host;
    if (init && init->tag == EXPR_CLASS_DECL) init->class_decl->expando_host = host;
    add_expando_namespace_decls(me, object, top_level);
    // The member (bindPotentiallyNewExpandoMemberToNamespace)
    bool merged;
    Symbol* member = declare_symbol(me, host->exports, name.sv, name, SYMBOL_PROPERTY, 0, false, &merged);
    if (!merged) {
        member->name = name.sv;
        member->name_span = name;
        member->parent = host;
        member->exported = true;
    }
    Expr* value = &op->right;
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    symbol_add_decl(member, (SymbolDecl){
        .kind = SYMBOL_DECL_ASSIGNMENT, .flags = SYMBOL_PROPERTY, .node = value, .name = left->outer_span,
        .full = left->outer_span,
    });
}

// Every assignment of a chain (`f.a = f.b = 1`)
static void add_expando_chain(Scope* me, Expr* e, bool top_level) {
    for (; e->tag == EXPR_BINARY_OP && e->binary_op->op == BINARY_OP_EQUAL; e = &e->binary_op->right) {
        if (expr_is_perenthisized(e)) break;
        add_expando(me, e->binary_op, top_level);
    }
}

void scope_add_expandos(Scope* me, Block const* block) {
    bool top_level = my kind == SCOPE_MODULE || my kind == SCOPE_SCRIPT;
    for_each (i, vec_statement, &block->statements) {
        if (i->tag == STATEMENT_EXPR) add_expando_chain(me, &i->expr, top_level);
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
    }
    return nullptr;
}

// Declares an assignment member `name` (`left` = v) in a class's or constructor function's table
static void declare_assignment_member(
    Scope* me, SymbolTable** table, Symbol* parent, Span name, Expr* left, Expr* value, AssignmentDeclKind kind
) {
    if (!*table) *table = arena_new(&my binder->arena, SymbolTable);
    bool merged;
    Symbol* member = declare_symbol(me, *table, name.sv, name, SYMBOL_PROPERTY, 0, false, &merged);
    if (!merged) {
        member->name = name.sv;
        member->name_span = name;
        member->parent = parent;
        member->exported = true;
    }
    symbol_add_decl(member, (SymbolDecl){
        .kind = SYMBOL_DECL_ASSIGNMENT, .assignment = kind, .flags = SYMBOL_PROPERTY, .node = value,
        .name = left->outer_span, .full = left->outer_span,
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
    if (!span_valid(name)) return;
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
    ClassItem* field = class_decl ? class_decl_find_member(class_decl, name.sv, is_static) : nullptr;
    if (field) field->field.ts_assigned = true;
}

void scope_add_this_property(Scope* me, BinaryOpExpr* op) {
    Expr* left = &op->left;
    Span name = expr_static_property_name(left);
    // (Private names must be declared, even in JavaScript)
    if (!span_valid(name) || csview_starts_with(name.sv, "#")) return;
    Expr const* object = &left->property->expr;
    if (object->tag != EXPR_IDENTIFIER || object->identifier.keyword != KEYWORD_IDENTIFIER_THIS || expr_is_perenthisized(object)) {
        return;
    }
    Expr* value = &op->right;
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    // The function `this` belongs to (arrow functions have their container's)
    FunctionImpl const* function = nullptr;
    for (Scope const* scope = me; scope && !function; scope = scope->parent) {
        if (scope->kind == SCOPE_FUNCTION && scope->function && !span_valid(scope->function->arrow_span)) function = scope->function;
        if (scope->kind == SCOPE_STATIC_BLOCK || scope->kind == SCOPE_MODULE || scope->kind == SCOPE_SCRIPT) break;
    }
    IdentifierExpr const* this = &object->identifier;
    switch (this->this_kind) {
        case THIS_CLASS_INSTANCE: {
            Symbol* class_symbol = (Symbol*)this->target;
            if (!class_symbol) return;
            AssignmentDeclKind kind = function && function->is_constructor ?
                ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR : ASSIGNMENT_DECL_THIS_IN_METHOD;
            declare_assignment_member(me, &class_symbol->members, class_symbol, name, left, value, kind);
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
            declare_assignment_member(me, &class_symbol->exports, class_symbol, name, left, value, ASSIGNMENT_DECL_THIS_IN_METHOD);
        }   break;
        case THIS_ANY: {
            FunctionImpl* this_function = (FunctionImpl*)this->this_function;
            if (!this_function || this_function != function) return;
            // `X.prototype.m = function () { this.x = v }` declares x for X, else the function is a constructor
            Symbol* class_symbol = nullptr;
            AssignmentDeclKind kind = ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR;
            Expr const* assigned_to = this_function->this_assigned_object;
            Span prototype = assigned_to ? expr_static_property_name(assigned_to) : span_null;
            if (span_valid(prototype) && span_equals(prototype, "prototype") &&
                assigned_to->property->expr.tag == EXPR_IDENTIFIER
            ) {
                class_symbol = js_constructor_symbol((Symbol*)assigned_to->property->expr.identifier.target);
                kind = ASSIGNMENT_DECL_THIS_IN_METHOD;
            } else if (!assigned_to) {
                class_symbol = this_function->class_symbol ? this_function->class_symbol :
                    this_function->is_expression ? nullptr : this_function->symbol;
            }
            if (!class_symbol) return;
            class_symbol->flags |= SYMBOL_CLASS;
            declare_assignment_member(me, &class_symbol->members, class_symbol, name, left, value, kind);
        }   break;
        default: break;
    }
}
