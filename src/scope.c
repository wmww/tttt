#include "scope.h"
#include "several_types.h"
#include "error.h"
#include "source_file.h"
#include "generic.h"
#include "program.h"
#include "port_statement.h"
#include "ty/ty.h"

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
static void symbol_table_collect_into(SymbolTable const* me, SymbolTable const* top, Symbol** out, u32* count) {
    if (!me) return;
    symbol_table_collect_into(my parent, top, out, count);
    for (u32 i = 0; i < my count; i++) {
        bool shadowed = false;
        for (SymbolTable const* above = top; above != me && !shadowed; above = above->parent) {
            shadowed = symbol_table_get_own(above, my entries[i].name, my entries[i].hash);
        }
        if (!shadowed) out[(*count)++] = my entries[i].symbol;
    }
}

Symbol** symbol_table_collect(SymbolTable const* me, u32* count) {
    *count = 0;
    u32 total = 0;
    for (SymbolTable const* i = me; i; i = i->parent) total += i->count;
    if (!total) return nullptr;
    Symbol** ret = malloc(sizeof(Symbol*) * total);
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
    for (u32 i = 0; i < my symbol_count; i++) {
        declaration_drop(&my symbols[i]->decl);
        lazy_type_drop(&my symbols[i]->value_interface);
    }
    free(my symbols);
    arena_drop(&my arena);
    free(me);
}

void binder_set_parent(Binder* me, Binder const* parent) {
    check(!my globals.parent);
    my globals.parent = &parent->globals;
}

Symbol* binder_new_symbol(Binder* me, csview name, u32 flags) {
    Symbol* ret = arena_new(&my arena, Symbol);
    ret->name = name;
    ret->flags = flags;
    ret->binder = me;
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

static void symbol_ensure_value_interface(Symbol* me) {
    if (!my value_interface.tag) my value_interface = lazy_type_init(LAZY_TYPE_SCOPE_VALUE_INTERFACE);
}

Symbol* binder_new_module_symbol(Binder* me, SourceFile* file) {
    Symbol* ret = binder_new_symbol(me, c_sv(""), SYMBOL_VALUE_MODULE);
    ret->is_module = true;
    ret->module_file = file;
    symbol_ensure_exports(ret);
    symbol_ensure_value_interface(ret);
    declaration_init(&ret->decl, (DeclProps){
        .name = sv_make_span(c_sv("")),
        .tag = DECLARATION_NAMESPACE_WITH_VALUE,
        .value_type = type_lazy(&ret->value_interface),
        .namespace = ret,
    });
    return ret;
}

// Stands for something that couldn't be resolved: has every meaning, and everything about it is any
static Symbol* binder_new_placeholder(Binder* me, Span name, Span full) {
    Symbol* ret = binder_new_symbol(
        me, name.sv, SYMBOL_FUNCTION_SCOPED_VARIABLE | SYMBOL_TYPE_ALIAS | SYMBOL_NAMESPACE_MODULE
    );
    ret->is_placeholder = true;
    ret->decl = (Declaration){
        .name = name,
        .declarations = vec_span_single(full),
        .value_tag = DECLARATION_NAMESPACE_WITH_VALUE,
        .type_tag = DECLARATION_NAMESPACE_WITH_VALUE,
        .namespace_tag = DECLARATION_NAMESPACE_WITH_VALUE,
    };
    return ret;
}

static void symbol_add_decl_name(Symbol* me, Span name) {
    if (my decl_count == my decl_cap) {
        u32 cap = my decl_cap ? my decl_cap * 2 : 2;
        Span* names = arena_new_n(&my binder->arena, Span, cap);
        if (my decl_count) memcpy(names, my decl_names, sizeof(Span) * my decl_count);
        my decl_names = names;
        my decl_cap = cap;
    }
    my decl_names[my decl_count++] = name;
}

// Scopes

Scope* scope_new(Scope* parent, ScopeKind kind) {
    Scope* ret = arena_new(&parent->binder->arena, Scope);
    ret->parent = parent;
    ret->kind = kind;
    ret->binder = parent->binder;
    ret->errs = parent->errs;
    ret->ambient = parent->ambient;
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
    bool same_file = existing->decl_count && existing->decl_names[0].source == name.source;
    unsigned code = 2300;
    if ((existing->flags | includes) & SYMBOL_ENUM) {
        code = 2567;
    } else if (existing->flags & SYMBOL_BLOCK_SCOPED_VARIABLE) {
        code = 2451;
    } else if (!same_file && includes & SYMBOL_BLOCK_SCOPED_VARIABLE) {
        // Across files symbols are merged by the checker in tsc, which looks at both sides
        code = 2451;
    }
    for (u32 i = 0; i < existing->decl_count; i++) report_conflict(errs, existing->decl_names[i], code);
    report_conflict(errs, name, code);
}

// A program that adds to a symbol of the lib program it shares (an interface, a namespace like Intl) gets a clone to
// add to. What's inside the lib keeps referring to the original, see hacks.md
static Symbol* clone_parent_symbol(Binder* binder, Symbol const* original) {
    Symbol* ret = binder_new_symbol(binder, original->name, original->flags);
    Binder* owner = ret->binder;
    *ret = *original;
    ret->binder = owner;
    declaration_clone_in_place(&ret->decl);
    if (ret->decl.namespace == original) ret->decl.namespace = ret;
    if (original->decl_count) {
        ret->decl_names = arena_new_n(&binder->arena, Span, original->decl_cap);
        memcpy(ret->decl_names, original->decl_names, sizeof(Span) * original->decl_count);
    }
    if (original->exports) {
        ret->exports = arena_new(&binder->arena, SymbolTable);
        ret->exports->parent = original->exports;
    }
    if (original->value_interface.tag) {
        ret->value_interface = lazy_type_init(LAZY_TYPE_SCOPE_VALUE_INTERFACE);
        // The clone's value is a type of its own
        if (ret->decl.merged_value_type.lazy.tag == LAZY_TYPE_EVALUATED &&
            ret->decl.merged_value_type.lazy.preevaluated.tag == TYPE_LAZY &&
            ret->decl.merged_value_type.lazy.preevaluated.lazy == &original->value_interface
        ) {
            ret->decl.merged_value_type.lazy.preevaluated.lazy = &ret->value_interface;
        }
        for_each (i, vec_type, &ret->decl.merged_value_type.merge_list) {
            if (i->tag == TYPE_LAZY && i->lazy == &original->value_interface) i->lazy = &ret->value_interface;
        }
    }
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

static void declaration_set_namespaces(Declaration* decl, Symbol const* parent) {
    if (!parent || parent->is_module) return;
    declaration_set_namespaces(decl, parent->parent);
    vec_span_push(&decl->namespaces, parent->decl.name);
}

static void symbol_flags_of_props(DeclProps const* props, u32* includes, u32* excludes) {
    switch (props->tag) {
        case DECLARATION_NAMESPACE_TYPE_ONLY:
            *includes = SYMBOL_NAMESPACE_MODULE;
            *excludes = SYMBOL_NAMESPACE_MODULE_EXCLUDES;
            return;
        case DECLARATION_NAMESPACE_WITH_VALUE:
            *includes = SYMBOL_VALUE_MODULE;
            *excludes = SYMBOL_VALUE_MODULE_EXCLUDES;
            return;
        case DECLARATION_FUNCTION_IMPL:
        case DECLARATION_FUNCTION_SIGNATURE:
            *includes = SYMBOL_FUNCTION;
            *excludes = SYMBOL_FUNCTION_EXCLUDES;
            return;
        case DECLARATION_VARIABLE:
            *includes = props->function_scoped ? SYMBOL_FUNCTION_SCOPED_VARIABLE : SYMBOL_BLOCK_SCOPED_VARIABLE;
            *excludes = props->function_scoped ?
                SYMBOL_FUNCTION_SCOPED_VARIABLE_EXCLUDES : SYMBOL_BLOCK_SCOPED_VARIABLE_EXCLUDES;
            return;
        case DECLARATION_ARGUMENT:
            *includes = SYMBOL_FUNCTION_SCOPED_VARIABLE;
            *excludes = SYMBOL_PARAMETER_EXCLUDES;
            return;
        case DECLARATION_INTERFACE:
            *includes = SYMBOL_INTERFACE;
            *excludes = SYMBOL_INTERFACE_EXCLUDES;
            return;
        case DECLARATION_TYPE:
            *includes = props->is_infer ? SYMBOL_TYPE_PARAMETER : SYMBOL_TYPE_ALIAS;
            *excludes = props->is_infer ? SYMBOL_TYPE_PARAMETER_EXCLUDES : SYMBOL_TYPE_ALIAS_EXCLUDES;
            return;
        case DECLARATION_GENERIC_TYPE:
            *includes = SYMBOL_TYPE_PARAMETER;
            *excludes = SYMBOL_TYPE_PARAMETER_EXCLUDES;
            return;
        case DECLARATION_CLASS:
            *includes = SYMBOL_CLASS;
            *excludes = SYMBOL_CLASS_EXCLUDES;
            return;
        case DECLARATION_ENUM:
            *includes = props->is_const_enum ? SYMBOL_CONST_ENUM : SYMBOL_REGULAR_ENUM;
            *excludes = props->is_const_enum ? SYMBOL_CONST_ENUM_EXCLUDES : SYMBOL_REGULAR_ENUM_EXCLUDES;
            return;
        case DECLARATION_ENUM_CASE:
            *includes = SYMBOL_ENUM_MEMBER;
            *excludes = SYMBOL_ENUM_MEMBER_EXCLUDES;
            return;
        case DECLARATION_ENUM_CASE_TYPE:
        case DECLARATION_NONE:
            break;
    }
    panic("declaration tag ", (i32)props->tag, " can't be declared in a scope");
}

void scope_init_declaration(Scope* me, DeclHandle* ret, DeclProps props) {
    if (ret->get) {
        // `var`s of nested blocks were declared by statement_hoist_names() already
        if (!props.function_scoped) log_error("Declaration ", ret->get->name, " acquired multiple times");
        type_drop(&props.value_type);
        type_drop(&props.declared_type);
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
    if (merged && includes & flags_before & SYMBOL_TYPE_PARAMETER) {
        // Type parameters of the same name merge as far as tsc's binder is concerned
        if (props.is_infer) {
            // `infer U` twice in one conditional type: the same type parameter
            type_drop(&props.declared_type);
            *ret = (DeclHandle){ .get = &symbol->decl, .symbol = symbol };
            return;
        }
        // <T, T>: the checker reports the second
        report_conflict(scope->errs, props.name, 2300);
        symbol = binder_new_symbol(scope->binder, props.name.sv, includes);
        merged = false;
    }
    if (merged && includes & (SYMBOL_CLASS | SYMBOL_FUNCTION) && flags_before & SYMBOL_VALUE_MODULE &&
        !(flags_before & (SYMBOL_CLASS | SYMBOL_FUNCTION)) && !scope->ambient &&
        symbol->decl_count && symbol->decl_names[0].source == props.name.source &&
        (props.tag != DECLARATION_FUNCTION_SIGNATURE)
    ) {
        error_ctx_error_d(scope->errs, symbol->decl_names[0], 2434,
            "A namespace declaration cannot be located prior to a class or function with which it is merged.");
    }
    if (!merged) {
        // Like in tsc only exports have a parent: a local of a namespace is just `x`, not `N.x`
        if (scope->container && table == scope->container->exports) symbol->parent = scope->container;
        symbol->decl.name = props.name;
        symbol->decl.exported = props.exported;
        declaration_set_namespaces(&symbol->decl, symbol->parent);
    }
    symbol_add_decl_name(symbol, props.name);

    if (includes & (SYMBOL_NAMESPACE_MODULE | SYMBOL_VALUE_MODULE | SYMBOL_ENUM)) {
        props.namespace = symbol;
        symbol_ensure_exports(symbol);
    }
    if (includes & SYMBOL_VALUE_MODULE) {
        // The value of a namespace is one type however many blocks it has
        if (symbol->decl.namespace_tag == DECLARATION_NAMESPACE_WITH_VALUE) {
            props.value_known = true;
        } else {
            symbol_ensure_value_interface(symbol);
            props.value_type = type_lazy(&symbol->value_interface);
        }
    }
    if (merged && includes & SYMBOL_FUNCTION_SCOPED_VARIABLE && flags_before & SYMBOL_FUNCTION_SCOPED_VARIABLE) {
        // `var a; var a;` (or a parameter and a var): the first one gives the type. TODO: TS2403
        type_drop(&props.value_type);
        props.value_type = (TypeNode){0};
        props.value_known = true;
    }
    declaration_init(&symbol->decl, props);
    *ret = (DeclHandle){ .get = &symbol->decl, .symbol = symbol };
}

// Ports

static bool port_item_declares_export(PortItem const* item) {
    return port_item_is_export(item) || item->export_import;
}

void scope_add_port(Scope* me, PortItem* item) {
    switch (item->tag) {
        case PORT_NOOP:
        case PORT_EXPORT_AS_NAMESPACE:
            return;

        case PORT_REEXPORT_SPLAT: {
            Symbol* module = my container;
            if (!module || !module->is_module || !item->source_file) return;
            if (module->export_star_count == module->export_star_cap) {
                u32 cap = module->export_star_cap ? module->export_star_cap * 2 : 4;
                PortItem** stars = arena_new_n(&my binder->arena, PortItem*, cap);
                if (module->export_star_count) {
                    memcpy(stars, module->export_stars, sizeof(PortItem*) * module->export_star_count);
                }
                module->export_stars = stars;
                module->export_star_cap = cap;
            }
            module->export_stars[module->export_star_count++] = item;
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
        symbol->decl.name = item->decl_name;
        symbol->decl.exported = exported;
    }
    symbol_add_decl_name(symbol, item->decl_name);
    vec_span_push(&symbol->decl.declarations, item->full_span);
    symbol->alias_item = item;
    symbol->alias_scope = me;
    item->symbol = symbol;

    bool needs_file = item->tag != PORT_IMPORT_ALIAS && item->tag != PORT_EXPORT_SPECIFIC;
    if (needs_file && !item->source_file) {
        // TS2307 was reported by the statement
        symbol->alias_target = binder_new_placeholder(my binder, item->decl_name, item->full_span);
        symbol->alias_state = ALIAS_DONE;
    }
}

static bool scope_default_export_table(Scope* me, Span at, Symbol const* symbol, u32 flags, SymbolTable** table) {
    if (my kind != SCOPE_MODULE || !my container) return false; // TODO: TS1319 and friends
    symbol_ensure_exports(my container);
    *table = my container->exports;
    Symbol* existing = symbol_table_get(*table, c_sv("default"));
    if (existing && (existing == symbol || !(existing->flags & flags & SYMBOL_ANY_MEANING))) {
        return false; // Overloads, or an interface and a value. TODO: the second one is lost
    }
    if (existing) {
        for (u32 i = 0; i < existing->decl_count; i++) {
            if (span_valid(existing->decl_names[i])) {
                error_ctx_error_d(my errs, existing->decl_names[i], 2528, "A module cannot have multiple default exports.");
            }
        }
        if (span_valid(at)) error_ctx_error_d(my errs, at, 2528, "A module cannot have multiple default exports.");
        return false;
    }
    return true;
}

void scope_export_default(Scope* me, Symbol* symbol) {
    SymbolTable* table;
    if (!scope_default_export_table(me, symbol->decl.name, symbol, symbol->flags, &table)) return;
    symbol_table_insert(table, &my binder->arena, c_sv("default"), symbol_name_hash(c_sv("default")), symbol);
}

void scope_export_default_expr(Scope* me, Expr* expr) {
    SymbolTable* table;
    if (!scope_default_export_table(me, expr->outer_span, nullptr, SYMBOL_VALUE, &table)) return;
    Symbol* symbol;
    if (expr->tag == EXPR_IDENTIFIER && !expr->identifier.keyword) {
        // Everything the name means is exported, types included
        symbol = binder_new_symbol(my binder, c_sv("default"), SYMBOL_ALIAS);
        symbol->alias_scope = me;
        symbol->alias_name = expr->identifier.name;
        symbol->decl.name = expr->identifier.name;
        vec_span_push(&symbol->decl.declarations, expr->outer_span);
    } else {
        symbol = binder_new_symbol(my binder, c_sv("default"), SYMBOL_BLOCK_SCOPED_VARIABLE);
        declaration_init(&symbol->decl, (DeclProps){
            .name = expr->outer_span,
            .full = expr->outer_span,
            .tag = DECLARATION_VARIABLE,
            .value_type = type_lazy(&expr->lazy_type),
        });
    }
    symbol->parent = my container;
    symbol_add_decl_name(symbol, expr->outer_span);
    symbol_table_insert(table, &my binder->arena, c_sv("default"), symbol_name_hash(c_sv("default")), symbol);
}

// Modules

static Symbol* source_file_module_symbol(SourceFile const* file) {
    return file && file->is_module && file->scope ? file->scope->container : nullptr;
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
    Symbol** own = symbol_table_collect(module->exports, &count);
    for (u32 i = 0; i < count; i++) {
        if (nested && csview_equals(own[i]->name, "default")) continue;
        u64 hash = symbol_name_hash(own[i]->name);
        if (!symbol_table_get_own(target, own[i]->name, hash)) {
            symbol_table_insert(target, arena, own[i]->name, hash, own[i]);
        }
    }
    free(own);

    for (u32 i = 0; i < module->export_star_count; i++) {
        Symbol* next = source_file_module_symbol(module->export_stars[i]->source_file);
        if (!next || module_visited(*visited, *visited_count, next)) continue;
        extend_export_stars(target, arena, next, visited, visited_count, visited_cap, true);
    }
}

static SymbolTable const* module_resolved_exports(Symbol* me) {
    if (!my export_star_count) return my exports;
    if (my resolved_exports_state == ALIAS_DONE) return my resolved_exports;
    SymbolTable* table = arena_new(&my binder->arena, SymbolTable);
    Symbol** visited = nullptr;
    u32 visited_count = 0;
    u32 visited_cap = 0;
    extend_export_stars(table, &my binder->arena, me, &visited, &visited_count, &visited_cap, false);
    free(visited);
    my resolved_exports = table;
    my resolved_exports_state = ALIAS_DONE;
    return table;
}

Symbol* symbol_get_export(Symbol const* me, csview name) {
    if (!me) return nullptr;
    SymbolTable const* table = my is_module ? module_resolved_exports((Symbol*)me) : my exports;
    return symbol_table_get(table, name);
}

Symbol** symbol_collect_exports(Symbol const* me, u32* count) {
    *count = 0;
    if (!me) return nullptr;
    return symbol_table_collect(my is_module ? module_resolved_exports((Symbol*)me) : my exports, count);
}

Declaration const* symbol_get_export_declaration(Symbol const* me, Span name) {
    Symbol const* member = symbol_get_export(me, name.sv);
    return member ? symbol_declaration(member, SYMBOL_ANY_MEANING) : nullptr;
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
    PortItem* item = my alias_item;
    ErrorCtx* errs = my alias_scope->errs;
    if (vec_span_is_empty(&item->alias_path)) return nullptr;
    Span first = *vec_span_front(&item->alias_path);
    Symbol* target;
    if (vec_span_size(&item->alias_path) == 1) {
        target = scope_resolve(my alias_scope, first.sv, SYMBOL_NAMESPACE);
        if (!target) target = scope_resolve(my alias_scope, first.sv, SYMBOL_VALUE);
        if (!target) target = scope_resolve(my alias_scope, first.sv, SYMBOL_TYPE);
        if (target == me) target = nullptr;
        if (!target) {
            error_ctx_error_d(errs, first, 2304, "Cannot find name '", first, "'.");
            return nullptr;
        }
        target = resolve_if_pure_alias(target);
        if (target) vec_symbol_cptr_push(&item->alias_path_symbols, target);
        return target;
    }

    target = scope_resolve(my alias_scope, first.sv, SYMBOL_NAMESPACE);
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
        Declaration const* container = symbol_declaration(target, SYMBOL_NAMESPACE);
        if (!container || !container->namespace) return nullptr; // A placeholder: anything goes
        Symbol* next = resolve_if_pure_alias(symbol_get_export(container->namespace, name.sv));
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
    PortItem* item = my alias_item;
    Symbol* ret = binder_new_symbol(my binder, item->decl_name.sv, SYMBOL_VALUE_MODULE);
    declaration_init(&item->wrapped_declaration, (DeclProps){
        .name = item->decl_name,
        .full = item->full_span,
        .tag = DECLARATION_TYPE,
        .declared_type = type_lazy(&module->value_interface),
        .is_typeof = true,
    });
    ret->decl.name = item->decl_name;
    declaration_init(&ret->decl, (DeclProps){
        .name = item->decl_name,
        .full = item->full_span,
        .tag = DECLARATION_NAMESPACE_WITH_VALUE,
        .value_type = type_identifier(c_new(IdentifierType, {
            .name = item->decl_name,
            .decl_ref = &item->wrapped_declaration,
        })),
        .namespace = module,
    });
    return ret;
}

static Symbol* resolve_alias_target(Symbol* me) {
    PortItem* item = my alias_item;
    ErrorCtx* errs = my alias_scope->errs;
    if (!item) {
        // `export default x`, TS2304 comes from the expression
        Symbol* found = scope_resolve(my alias_scope, my alias_name.sv, SYMBOL_ANY_MEANING);
        return found == me ? nullptr : resolve_if_pure_alias(found);
    }
    Symbol* module = nullptr;
    if (item->tag != PORT_IMPORT_ALIAS && item->tag != PORT_EXPORT_SPECIFIC) {
        module = source_file_module_symbol(item->source_file);
        if (!module) {
            // TODO: `export =` / ambient module files, the span is the module specifier's in tsc
            error_ctx_error_d(errs, item->decl_name, 2306,
                "File '", item->source_file->canonical_path, "' is not a module.");
            return nullptr;
        }
    }
    switch (item->tag) {
        case PORT_IMPORT_SPECIFIC:
        case PORT_REEXPORT_SPECIFIC: {
            Symbol* found = symbol_get_export(module, item->source_name.sv);
            if (!found) {
                error_ctx_error_d(errs, item->source_name, 2305,
                    "Module '", item->source_file->canonical_path, "' has no exported member '",
                    item->source_name, "'.");
                return nullptr;
            }
            item->source_symbol = found;
            return resolve_if_pure_alias(found);
        }

        case PORT_IMPORT_DEFAULT: {
            Symbol* found = symbol_get_export(module, c_sv("default"));
            if (!found) {
                // TODO: `export =` modules, where esModuleInterop makes the default import the whole module
                if (compiler_options()->allow_synthetic_default_imports && item->source_file->is_declaration) {
                    return nullptr;
                }
                error_ctx_error_d(errs, item->decl_name, 1192,
                    "Module '", item->source_file->canonical_path, "' has no default export.");
                return nullptr;
            }
            item->source_symbol = found;
            return resolve_if_pure_alias(found);
        }

        case PORT_EXPORT_SPECIFIC: {
            Symbol* found = scope_resolve(my alias_scope, item->source_name.sv, SYMBOL_ANY_MEANING);
            if (!found || found == me) {
                error_ctx_error_d(errs, item->source_name, 2304, "Cannot find name '", item->source_name, "'.");
                return nullptr;
            }
            item->source_symbol = found;
            return resolve_if_pure_alias(found);
        }

        case PORT_IMPORT_GATHER:
        case PORT_REEXPORT_GATHER:
            return make_gather_target(me, module);

        case PORT_IMPORT_ALIAS:
            return resolve_import_alias_path(me);

        case PORT_NOOP:
        case PORT_EXPORT_AS_NAMESPACE:
        case PORT_REEXPORT_SPLAT:
            break;
    }
    impossible;
}

Symbol* symbol_resolve_alias(Symbol* me) {
    if (!me || !(my flags & SYMBOL_ALIAS)) return me;
    switch (my alias_state) {
        case ALIAS_DONE: return my alias_target;
        case ALIAS_IN_FLIGHT: return nullptr; // TODO: TS2303 circular definition of import alias
        case ALIAS_UNRESOLVED: break;
    }
    my alias_state = ALIAS_IN_FLIGHT;
    Symbol* target = resolve_alias_target(me);
    if (!target) {
        target = my alias_item ?
            binder_new_placeholder(my binder, my alias_item->decl_name, my alias_item->full_span) :
            binder_new_placeholder(my binder, my alias_name, my alias_name);
    }
    my alias_target = target;
    my alias_state = ALIAS_DONE;
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

Declaration const* symbol_declaration(Symbol const* me, u32 meaning) {
    for (int depth = 0; me && depth < ALIAS_CHAIN_MAX; depth++) {
        if (my flags & meaning || my is_placeholder) return &my decl;
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
        if (!(symbol->flags & SYMBOL_ALIAS) || !symbol->alias_scope) continue;
        Symbol* target = symbol_resolve_alias(symbol);
        if (!symbol->alias_item || !target || target->is_placeholder) continue;
        // In tsc an exported declaration also has a local symbol, which an import of the same name conflicts with
        u32 local_flags = symbol->flags;
        Symbol const* container = symbol->alias_scope->container;
        if (container && !port_item_declares_export(symbol->alias_item)) {
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
        PortItem const* item = symbol->alias_item;
        ErrorCtx* errs = symbol->alias_scope->errs;
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
    return my flags == SYMBOL_ALIAS && my alias_item && port_item_is_export(my alias_item);
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

static Declaration const* scope_get(Scope const* me, csview name, u32 meaning, Symbol const** found) {
    Symbol* symbol = scope_resolve(me, name, meaning);
    if (found) *found = symbol;
    return symbol_declaration(symbol, meaning);
}

Declaration const* scope_get_value(Scope const* me, csview name, Symbol const** found) {
    return scope_get(me, name, SYMBOL_VALUE, found);
}

Declaration const* scope_get_type(Scope const* me, csview name, Symbol const** found) {
    return scope_get(me, name, SYMBOL_TYPE, found);
}

Declaration const* scope_get_namespace(Scope const* me, csview name, Symbol const** found) {
    return scope_get(me, name, SYMBOL_NAMESPACE, found);
}

Declaration const* scope_get_keyword(Scope const* me, KeywordIdentifier keyword, bool type_else_value) {
    for (Scope const* scope = me; scope; scope = scope->parent) {
        Declaration const* decl = nullptr;
        switch (keyword) {
            case KEYWORD_IDENTIFIER_THIS: decl = scope->this_declaration; break;
            case KEYWORD_IDENTIFIER_SUPER: decl = scope->super_declaration; break;
            case KEYWORD_IDENTIFIER_NONE: break;
        }
        if (decl) {
            if (type_else_value && decl->type_tag) {
                return decl;
            } else if (!type_else_value && decl->value_tag) {
                return decl;
            }
        }
    }
    return nullptr;
}

Ty const* symbol_evaluate_value_interface(Symbol const* me) {
    return ty_namespace_type(current_checker, me);
}
