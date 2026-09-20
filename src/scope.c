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
#include "statement.h"

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
    for (u32 i = 0; i < my decl_count; i++) {
        if (span_valid(my decls[i].full)) vec_span_push(&ret, my decls[i].full);
    }
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
    if (kind == SCOPE_BLOCK) ret->function = parent->function;
    return ret;
}

Scope* scope_new_file(Binder* binder, SourceFile* file) {
    Scope* ret = scope_new(binder->global_scope, file->is_module ? SCOPE_MODULE : SCOPE_SCRIPT);
    ret->ambient = file->is_declaration;
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

bool scope_has_function(Scope const* me) {
    for (Scope const* i = me; i; i = i->parent) {
        if (i->kind == SCOPE_FUNCTION) return true;
    }
    return false;
}

// Declaring

static void report_conflict(ErrorCtx* errs, Span at, unsigned code) {
    if (!span_valid(at) || !at.source || at.source->is_stdlib) return;
    switch (code) {
        case 2567:
            error_ctx_error_d(errs, at, 2567,
                "Enum declarations can only merge with namespace or other enum declarations.");
            break;
        case 2451:
            error_ctx_error_d(errs, at, 2451, "Cannot redeclare block-scoped variable '", at, "'.");
            break;
        default:
            error_ctx_error_d(errs, at, 2300, "Duplicate identifier '", at, "'.");
            break;
    }
}

static void report_conflicts(ErrorCtx* errs, Symbol const* existing, Span name, u32 includes) {
    bool same_file = existing->decl_count && existing->decls[0].name.source == name.source;
    unsigned code = 2300;
    if ((existing->flags | includes) & SYMBOL_ENUM) {
        code = 2567;
    } else if (existing->flags & SYMBOL_BLOCK_SCOPED_VARIABLE) {
        code = 2451;
    } else if (!same_file && includes & SYMBOL_BLOCK_SCOPED_VARIABLE) {
        // Across files symbols are merged by the checker in tsc, which looks at both sides
        code = 2451;
    }
    for (u32 i = 0; i < existing->decl_count; i++) report_conflict(errs, existing->decls[i].name, code);
    report_conflict(errs, name, code);
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

// tsc's declareSymbol(). *merged is whether the symbol existed already
static Symbol* declare_symbol(
    Scope* scope, SymbolTable* table, csview name, Span name_span, u32 includes, u32 excludes, bool* merged
) {
    Binder* binder = scope->binder;
    u64 hash = symbol_name_hash(name);
    *merged = false;
    Symbol* existing = symbol_table_get_own(table, name, hash);
    if (!existing && table->parent) {
        Symbol* inherited = symbol_table_lookup(table->parent, name, hash);
        if (inherited) {
            if (inherited->flags & excludes) {
                report_conflicts(scope->errs, inherited, name_span, includes);
                return binder_new_symbol(binder, name, includes);
            }
            existing = clone_parent_symbol(binder, inherited);
            symbol_table_insert(table, &binder->arena, existing->name, hash, existing);
        }
    }
    if (existing) {
        if (existing->flags & excludes) {
            report_conflicts(scope->errs, existing, name_span, includes);
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

    u32 includes, excludes;
    symbol_flags_of_props(&props, &includes, &excludes);
    SymbolTable* table = scope_declaration_table(scope, props.exported);
    bool merged;
    Symbol* symbol = declare_symbol(scope, table, props.name.sv, props.name, includes, excludes, &merged);
    u32 flags_before = merged ? symbol->decl_flags_before : 0;
    if (merged && includes & flags_before & SYMBOL_TYPE_PARAMETER && props.kind != SYMBOL_DECL_INFER) {
        // Type parameters of the same name merge as far as tsc's binder is concerned, which is what `infer U` twice
        // in one conditional type wants. <T, T>: the checker reports the second
        report_conflict(scope->errs, props.name, 2300);
        symbol = binder_new_symbol(scope->binder, props.name.sv, includes);
        merged = false;
    }
    if (merged && includes & (SYMBOL_CLASS | SYMBOL_FUNCTION) && flags_before & SYMBOL_VALUE_MODULE &&
        !(flags_before & (SYMBOL_CLASS | SYMBOL_FUNCTION)) && !scope->ambient &&
        symbol->decl_count && symbol->decls[0].name.source == props.name.source &&
        !(props.kind == SYMBOL_DECL_FUNCTION && !((FunctionImpl const*)props.node)->has_body)
    ) {
        error_ctx_error_d(scope->errs, symbol->decls[0].name, 2434,
            "A namespace declaration cannot be located prior to a class or function with which it is merged.");
    }
    if (!merged) {
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
        .kind = props.kind, .flags = includes, .node = props.node, .name = props.name, .full = props.full,
    });
    if (includes & (SYMBOL_NAMESPACE_MODULE | SYMBOL_VALUE_MODULE | SYMBOL_ENUM)) symbol_ensure_exports(symbol);
    *ret = symbol;
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
            bool merged;
            Symbol* symbol = declare_symbol(
                me, &my binder->globals, item->decl_name.sv, item->decl_name, SYMBOL_ALIAS, SYMBOL_ALIAS_EXCLUDES, &merged
            );
            if (merged) return;
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

    bool exported = port_item_declares_export(item);
    if (port_item_is_export(item) && !(my container && my kind != SCOPE_ENUM)) {
        return; // TODO: TS1194 export declarations are not permitted in a namespace... and nowhere else either
    }
    SymbolTable* table = scope_declaration_table(me, exported);
    bool merged;
    Symbol* symbol = declare_symbol(
        me, table, item->decl_name.sv, item->decl_name, SYMBOL_ALIAS, SYMBOL_ALIAS_EXCLUDES, &merged
    );
    if (!merged) {
        symbol->parent = my container;
        symbol->name_span = item->decl_name;
        symbol->exported = exported;
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

static bool scope_default_export_table(
    Scope* me, csview name, Span at, Symbol const* symbol, u32 flags, SymbolTable** table
) {
    if (!my container || !my container->is_module) return false; // TODO: TS1319 and friends
    symbol_ensure_exports(my container);
    *table = my container->exports;
    Symbol* existing = symbol_table_get(*table, name);
    if (existing && !csview_equals(name, "default")) return false; // TODO: TS2300 for a second `export =`
    if (existing && (existing == symbol || !(existing->flags & flags & SYMBOL_ANY_MEANING))) {
        return false; // Overloads, or an interface and a value. TODO: the second one is lost
    }
    if (existing) {
        for (u32 i = 0; i < existing->decl_count; i++) {
            if (span_valid(existing->decls[i].name)) {
                error_ctx_error_d(my errs, existing->decls[i].name, 2528, "A module cannot have multiple default exports.");
            }
        }
        if (span_valid(at)) error_ctx_error_d(my errs, at, 2528, "A module cannot have multiple default exports.");
        return false;
    }
    return true;
}

void scope_export_default(Scope* me, Symbol* symbol) {
    SymbolTable* table;
    if (!scope_default_export_table(me, c_sv("default"), symbol->name_span, symbol, symbol->flags, &table)) return;
    symbol_table_insert(table, &my binder->arena, c_sv("default"), symbol_name_hash(c_sv("default")), symbol);
}

void scope_export_default_expr(Scope* me, Expr* expr, bool export_equals) {
    SymbolTable* table;
    csview export_name = export_equals ? c_sv("export=") : c_sv("default");
    if (!scope_default_export_table(me, export_name, expr->outer_span, nullptr, SYMBOL_VALUE, &table)) return;
    Symbol* symbol;
    if (expr->tag == EXPR_IDENTIFIER && !expr->identifier.keyword) {
        // Everything the name means is exported, types included
        symbol = binder_new_symbol(my binder, export_name, SYMBOL_ALIAS);
        symbol->alias->scope = me;
        symbol->alias->name = expr->identifier.name;
        symbol->name_span = expr->identifier.name;
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_ALIAS, .flags = SYMBOL_ALIAS, .name = expr->outer_span, .full = expr->outer_span,
        });
    } else {
        symbol = binder_new_symbol(my binder, export_name, SYMBOL_BLOCK_SCOPED_VARIABLE);
        symbol->name_span = expr->outer_span;
        symbol_add_decl(symbol, (SymbolDecl){
            .kind = SYMBOL_DECL_EXPR, .flags = SYMBOL_BLOCK_SCOPED_VARIABLE, .node = expr,
            .name = expr->outer_span, .full = expr->outer_span,
        });
    }
    symbol->parent = my container;
    symbol_table_insert(table, &my binder->arena, export_name, symbol_name_hash(export_name), symbol);
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
        target = resolve_if_pure_alias(target);
        if (target) vec_symbol_cptr_push(&item->alias_path_symbols, target);
        return target;
    }

    target = scope_resolve(my alias->scope, first.sv, SYMBOL_NAMESPACE);
    if (target == me) target = nullptr;
    if (!target) {
        error_ctx_error_d(errs, first, 2503, "Cannot find namespace '", first, "'.");
        return nullptr;
    }
    target = resolve_if_pure_alias(target);
    if (target) vec_symbol_cptr_push(&item->alias_path_symbols, target);
    for (isize i = 1; target && i < vec_span_size(&item->alias_path); i++) {
        Span name = *vec_span_at(&item->alias_path, i);
        Span prev = *vec_span_at(&item->alias_path, i - 1);
        bool last = i == vec_span_size(&item->alias_path) - 1;
        Symbol const* container = symbol_for_meaning(target, SYMBOL_NAMESPACE);
        if (!container || container->is_placeholder) return nullptr; // A placeholder: anything goes
        Symbol* next = resolve_if_pure_alias(symbol_get_export(container, name.sv));
        if (!next || (!last && !(symbol_resolved_flags(next) & SYMBOL_NAMESPACE) && !next->is_placeholder)) {
            error_ctx_error_d(errs, name, 2694, "Namespace '", prev, "' has no exported member '", name, "'.");
            return nullptr;
        }
        target = next;
        vec_symbol_cptr_push(&item->alias_path_symbols, target);
    }
    return target;
}

// `import * as ns`: a namespace symbol of its own around the module, so that its type prints as `typeof ns` (tsc gets
// there through symbol accessibility, plan 04 stage 3)
static Symbol* make_gather_target(Symbol* me, Symbol* module) {
    PortItem* item = my alias->item;
    Symbol* ret = binder_new_symbol(my binder, item->decl_name.sv, SYMBOL_VALUE_MODULE);
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
    csview path = cstr_sv(&module->module->file->canonical_path);
    static const char* const extensions[] = { ".d.ts", ".ts", ".tsx", ".js", ".jsx", ".mts", ".cts", ".mjs", ".cjs" };
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
        return found == me ? nullptr : resolve_if_pure_alias(found);
    }
    Symbol* module = nullptr;
    Symbol* assigned = nullptr; // What its `export =` names, if it has one
    if (item->tag != PORT_IMPORT_ALIAS && item->tag != PORT_EXPORT_SPECIFIC && item->tag != PORT_EXPORT_AS_NAMESPACE) {
        module = binder_module_of_ref(my binder, item->module_ref);
        if (!module) {
            // No file: TS2307 comes from the statement
            if (item->module_ref->file) {
                error_ctx_error_d(errs, item->module_ref->span, 2306,
                    "File '", item->module_ref->file->canonical_path, "' is not a module.");
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
            Symbol* found = symbol_get_export(module, item->source_name.sv);
            if (!found && assigned) {
                if (assigned->is_placeholder) return nullptr;
                // TODO: properties of the type of what is assigned are importable too
                Symbol const* container = symbol_for_meaning(assigned, SYMBOL_NAMESPACE);
                if (container) found = symbol_get_export(container, item->source_name.sv);
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
                if (compiler_options()->allow_synthetic_default_imports &&
                    (!item->module_ref->file || item->module_ref->file->is_declaration)
                ) {
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
        case ALIAS_IN_FLIGHT: return nullptr; // TODO: TS2303 circular definition of import alias
        case ALIAS_UNRESOLVED: break;
    }
    my alias->state = ALIAS_IN_FLIGHT;
    Symbol* target = resolve_alias_target(me);
    if (!target) {
        target = my alias->item ?
            binder_new_placeholder(my binder, my alias->item->decl_name, my alias->item->full_span) :
            binder_new_placeholder(my binder, my alias->name, my alias->name);
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
        me = symbol_resolve_alias((Symbol*)me);
    }
    return nullptr;
}

void binder_link(Binder* me) {
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

Symbol* scope_resolve(Scope const* me, csview name, u32 meaning) {
    u64 hash = symbol_name_hash(name);
    for (Scope const* scope = me; scope; scope = scope->parent) {
        SymbolTable const* table = scope->kind == SCOPE_GLOBAL ? &scope->binder->globals : &scope->locals;
        if (table->count || table->parent) {
            Symbol* found = symbol_table_lookup(table, name, hash);
            if (found && symbol_has_meaning(found, meaning)) return found;
        }
        if (scope->container && scope->container->exports) {
            Symbol* found = symbol_table_lookup(scope->container->exports, name, hash);
            // `export { a }` must not find itself
            if (found && !is_export_specifier_alias(found) && symbol_has_meaning(found, meaning)) return found;
        }
    }
    return nullptr;
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
        bool is_class = kind == THIS_CLASS_INSTANCE || kind == THIS_CLASS_STATIC;
        if (!type_else_value || is_class) return scope->this_ref;
    }
    return (ThisRef){0};
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
