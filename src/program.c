#include "program.h"
#include "parser.h"
#include "block.h"
#include "filesystem.h"
#include "error.h"
#include "statement.h"
#include "port_statement.h"
#include "source_file.h"
#include "scope_stack.h"

#define i_key_class_names SourceFile, source_file
#define i_no_clone
#include "wrap_stc/box.h"

// Keep source files in a box so new ones can be added without invalidating old pointers
#define i_key_ssv
#define i_val_class_names BoxSourceFile, box_source_file
#define i_no_clone
#include "wrap_stc/hmap.h"

typedef SourceFile* SourceFilePtr;
#define i_key_names SourceFilePtr, source_file_ptr
#include "wrap_stc/vec.h"

static const csview lib_path = c_sv_const_init("thirdparty/lib");

typedef enum {
    PROGRAM_PHASE_LOADING = 0, // Files may be loaded
    PROGRAM_PHASE_RESOLVING_NAMES, // Names currently being resolved
    PROGRAM_PHASE_NAMES_RESOLVED, // Resolving names is done
    PROGRAM_PHASE_TYPECHECKING, // Typechecking has been started
    PROGRAM_PHASE_TYPECHECKED, // Typechecking has completed
} ProgramPhase;

static const char* program_phase_get_str(ProgramPhase phase) {
    switch (phase) {
        case PROGRAM_PHASE_LOADING: return "loading";
        case PROGRAM_PHASE_RESOLVING_NAMES: return "resolving names";
        case PROGRAM_PHASE_NAMES_RESOLVED: return "names resolved";
        case PROGRAM_PHASE_TYPECHECKING: return "typechecking";
        case PROGRAM_PHASE_TYPECHECKED: return "typecheck done";
    }
    UNREACHABLE;
}

#define ASSERT_PHASE(expected) \
    PANIC_IF( \
        my phase != expected, \
        "expected program phase to be %s, was actually %s", \
        program_phase_get_str(expected), \
        program_phase_get_str(my phase) \
    ) \

#define CHANGE_PHASE(from, to) ASSERT_PHASE(from); my phase = to;

typedef struct Program {
    ProgramPhase phase;
    bool loading_stdlib; // If we're loading standard library files instead of normal files
    ErrorCtx* errs; // unowned
    Fs const* fs;
    Scope global_scope;
    HmapStrBoxSourceFile source_files_map;
    VecSourceFilePtr source_files_ordered;
} Program;

static void program_add_builtins(Program* me) {
#define GLOBAL_VALUE(global_name, global_value) {\
    ArcDeclaration declaration = {0}; \
    scope_init_declaration(&my global_scope, &declaration, (DeclProps) { \
        .name = sv_make_span(c_sv(global_name)), \
        .tag = DECLARATION_VARIABLE, \
        .value_type = global_value, \
    } ); \
    arc_declaration_drop(&declaration); \
}

#define GLOBAL_TYPE(global_name, global_type) {\
    ArcDeclaration declaration = {0}; \
    scope_init_declaration(&my global_scope, &declaration, (DeclProps) { \
        .name = sv_make_span(c_sv(global_name)), \
        .tag = DECLARATION_TYPE, \
        .declared_type = global_type, \
    } ); \
    arc_declaration_drop(&declaration); \
}

    GLOBAL_VALUE("globalThis", type_any);

    GLOBAL_TYPE("any", type_any);
    GLOBAL_TYPE("unknown", type_unknown);
    GLOBAL_TYPE("never", type_never);
    GLOBAL_TYPE("void", type_void);
    GLOBAL_TYPE("object", type_object);
    GLOBAL_TYPE("boolean", type_boolean);
    GLOBAL_TYPE("number", type_number);
    GLOBAL_TYPE("string", type_string);
    GLOBAL_TYPE("intrinsic", type_any);
}

static SourceFile* program_add_source_idempotent(Program* me, csview path);

static bool get_import_path_of_index(Fs const* fs, csview path, cstr* ret) {
    *ret = cstr_from_sv(path);
    intptr_t base_len = cstr_size(ret);

    cstr_append_n(ret, &path_sep, 1);
    cstr_append_lit(ret, "index.d.ts");
    if (fs_is_reg_file(fs, cstr_sv(ret))) {
        return true;
    }
    cstr_resize(ret, base_len, '_');

    cstr_append_n(ret, &path_sep, 1);
    cstr_append_lit(ret, "index.ts");
    if (fs_is_reg_file(fs, cstr_sv(ret))) {
        return true;
    }

    cstr_drop(ret);
    return false;
}

static bool get_import_path_find_extension(Fs const* fs, csview path, cstr* ret) {
    *ret = cstr_from_sv(path);
    intptr_t base_len = cstr_size(ret);

    if (csview_ends_with(path, ".js")) {
        base_len -= 3;
        cstr_resize(ret, base_len, '_');
    }

    if (fs_is_directory(fs, cstr_sv(ret))) {
        cstr ret2;
        if (get_import_path_of_index(fs, cstr_sv(ret), &ret2)) {
            cstr_drop(ret);
            *ret = ret2;
            return true;
        }
    }

    cstr_append_lit(ret, ".d.ts");
    if (fs_is_reg_file(fs, cstr_sv(ret))) {
        return true;
    }
    cstr_resize(ret, base_len, '_');

    cstr_append_lit(ret, ".ts");
    if (fs_is_reg_file(fs, cstr_sv(ret))) {
        return true;
    }
    cstr_resize(ret, base_len, '_');

    cstr_drop(ret);
    return false;
}

static bool get_import_path_relative(Fs const* fs, csview cwd, csview path, cstr* ret) {
    cstr str = cstr_from_sv(cwd);
    cstr_append_n(&str, &path_sep, 1);
    cstr_append_sv(&str, path);

    if (get_import_path_find_extension(fs, cstr_sv(&str), ret)) {
        cstr_drop(&str);
        return true;
    }

    cstr_drop(&str);
    return false;
}

static bool get_node_modules_dir(Fs const* fs, csview path, cstr* ret) {
    cstr str = cstr_from_sv(path);
    while (true) {
        intptr_t start_len = cstr_size(&str);
        cstr_append_n(&str, &path_sep, 1);
        cstr_append_lit(&str, "node_modules");
        if (fs_is_directory(fs, cstr_sv(&str))) {
            *ret = str;
            return true;
        }
        cstr_resize(&str, start_len, '_');
        cstr_resize(&str, basename_of(cstr_sv(&str)).buf - cstr_str(&str), '_');
        if (cstr_size(&str) <= 2 && (
                cstr_empty(&str) ||
                cstr_equals(&str, ".") ||
                cstr_equals(&str, "..") ||
                (cstr_size(&str) == 1 && *cstr_str(&str) == path_sep)
            )
        ) {
            cstr_drop(&str);
            return false;
        }
    }
}

static bool get_import_path(Fs const* fs, csview cwd, csview path, cstr* ret) {
    if (csview_starts_with(path, "/")) {
        return get_import_path_find_extension(fs, path, ret);
    }

    if (get_import_path_relative(fs, cwd, path, ret)) {
        return true;
    }

    cstr node_modules_path;
    if (get_node_modules_dir(fs, cwd, &node_modules_path)) {
        intptr_t base_len = cstr_size(&node_modules_path);

        cstr_append_n(&node_modules_path, &path_sep, 1);
        cstr_append_lit(&node_modules_path, "@types");
        if (get_import_path_relative(fs, cstr_sv(&node_modules_path), path, ret)) {
            cstr_drop(&node_modules_path);
            return true;
        }
        cstr_resize(&node_modules_path, base_len, '_');

        if (get_import_path_relative(fs, cstr_sv(&node_modules_path), path, ret)) {
            cstr_drop(&node_modules_path);
            return true;
        }

        cstr_drop(&node_modules_path);
    }

    return false;
}

Program* program_new(ErrorCtx* errs, Fs const* fs) {
    ASSERT(errs);
    ASSERT(fs);
    Program* ret = c_new(Program, {
        .errs = errs,
        .fs = fs,
        .global_scope = scope_init(errs),
    });
    program_add_builtins(ret);
    return ret;
}

static void program_drop(Program* me) {
    scope_drop(&my global_scope);
    hmap_str_box_source_file_drop(&my source_files_map);
    vec_source_file_ptr_drop(&my source_files_ordered);
}

void program_destroy(Program* me) {
    if (me) {
        program_drop(me);
        free(me);
    }
}

static SourceFile* program_load_import_idempotent(Program* me, Span path_span, csview path) {
    ASSERT(span_valid(path_span));
    csview cwd = dirname_of(cstr_sv(&path_span.source->canonical_path));
    cstr source_path;
    if (get_import_path(my fs, cwd, path, &source_path)) {
        SourceFile* ret = program_add_source_idempotent(me, cstr_sv(&source_path));
        cstr_drop(&source_path);
        return ret;
    } else {
        return nullptr;
    }
}

// Called for each toplevel statement of each loaded file
static inline void program_handle_statement(Program* me, SourceFile* source, Statement* statement) {
    switch (statement->tag) {
        case STATEMENT_SOURCE_DIRECTIVE:
            switch (statement->source_directive->tag) {
                case SOURCE_DIRECTIVE_REFERENCE_PATH:
                case SOURCE_DIRECTIVE_REFERENCE_LIB:
                case SOURCE_DIRECTIVE_REFERENCE_TYPES:
                    program_load_import_idempotent(
                        me,
                        statement->source_directive->path_span,
                        cstr_sv(&statement->source_directive->path)
                    );
                    break;
            }
            break;

        case STATEMENT_IMPORT_EXPORT: {
            source_file_set_module_true(source, my errs);
            PortStatement* port = statement->import_export;
            SourceFile* imported = nullptr;
            if (!port->requires_target) {
                imported = source;
            } else if (span_valid(port->target_path_span)) {
                imported = program_load_import_idempotent(me, port->target_path_span, cstr_sv(&port->target_path));
            }
            port_statement_assign_source(port, imported);
        }   break;

        case STATEMENT_EXPORT_DEFAULT_EXPR:
            source_file_set_module_true(source, my errs);
            break;

        case STATEMENT_DECL_NAMESPACE:
            if (statement->decl.namespace.is_global) {
                statement->decl.namespace.global_scope = &my global_scope;
            }
            // Fallthrough

        case STATEMENT_DECL_VARIABLE:
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_ENUM:
        case STATEMENT_DECL_CLASS:
        case STATEMENT_DECL_TYPE:
        case STATEMENT_DECL_INTERFACE:
            if (statement->decl.props.exported) {
                source_file_set_module_true(source, my errs);
            }
            break;

        case STATEMENT_NOOP:
        case STATEMENT_BLOCK:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
        case STATEMENT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            break;
    }
}

// Loads the file at the given path, adds it to the program, and returns a SourceFile for it. Returns nullptr on error.
// Only loads each file once, even if paths aren't expressed identically. Calls itself recursively to load imports and
// referenced files.
static SourceFile* program_add_source_idempotent(Program* me, csview path) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);

    cstr canonical_path = relative_path_of(fs_get_cwd(my fs), path);

    auto cached_source = hmap_str_box_source_file_get(&my source_files_map, cstr_sv(&canonical_path));
    if (cached_source) {
        // A source file is already created for this path
        cstr_drop(&canonical_path);
        return cached_source->second.get;
    }

    // No source file is created for this path, load it
    cstr contents;
    if (!fs_read_entire_file(my fs, cstr_sv(&canonical_path), &contents)) {
        cstr_drop(&canonical_path);
        return nullptr;
    }

    SourceFile* source_file = c_new(
        SourceFile,
        source_file_make(cstr_clone(canonical_path), contents)
    );

    if (my loading_stdlib) {
        source_file->is_stdlib = true;
        csview filename = basename_of(cstr_sv(&source_file->canonical_path));
        cstr_take(&source_file->decl_override, cstr_from_fmt("lib.%.*s, --, --", c_SV(filename)));
    }

    parse_file(source_file, my errs);

    hmap_str_box_source_file_insert(&my source_files_map, canonical_path, box_source_file_from_ptr(source_file));

    c_foreach(i, vec_statement, source_file->ast.statements) {
        program_handle_statement(me, source_file, i.ref);
    }

    // Add here so imports come before importing file
    vec_source_file_ptr_push(&my source_files_ordered, source_file);

    return source_file;
}

SourceFile* program_load(Program* me, csview path) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    set_error_config(&my errs->config);

    // Add the source file to the program
    SourceFile* main_source = program_add_source_idempotent(me, path);
    if (!main_source) {
        PANIC_IF(my loading_stdlib, "Failed to load standard library file %.*s", c_SV(path));
        error_ctx_error(my errs, span_null, 6053, "File '%.*s' not found.", c_SV(path));
        // Continue with null
    }

    clear_error_config();
    return main_source;
}

SourceFile* program_load_stdlib(Program* me, csview name) {
    my loading_stdlib = true;
    cstr path = cstr_from_fmt("%.*s/%.*s.d.ts", c_SV(lib_path), c_SV(name));
    SourceFile* source = program_load(me, cstr_sv(&path));
    my loading_stdlib = false;
    cstr_drop(&path);
    return source;
}

void program_copy_globals_from(Program* me, Program const* other) {
    scope_copy_declarations_from(&my global_scope, &other->global_scope);
}

void program_resolve_names(Program* me) {
    CHANGE_PHASE(PROGRAM_PHASE_LOADING, PROGRAM_PHASE_RESOLVING_NAMES);
    my errs->phase = ERROR_PHASE_RESOLVE_NAMES;

    c_foreach(i, vec_source_file_ptr, my source_files_ordered) {
        SourceFile* source = *i.ref;
        block_add_names(&source->ast, source_file_get_scope_or(source, &my global_scope));
        c_foreach(i, vec_scope_cptr, source->global_scopes) {
            scope_copy_declarations_from(&my global_scope, *i.ref);
        }
    }

    c_foreach(i, vec_source_file_ptr, my source_files_ordered) {
        SourceFile* source = *i.ref;
        if (source->is_module) {
            scope_process_imports(&source->module_scope);
        }
    }

    ScopeStack* names = scope_stack_new();
    scope_stack_push_scope(names, &my global_scope);

    c_foreach(i, vec_source_file_ptr, my source_files_ordered) {
        SourceFile* source = *i.ref;
        if (source->is_module) scope_push_to_stack(&source->module_scope, names);
        block_lookup_names(&source->ast, names, my errs);
        if (source->is_module) scope_pop_from_stack(&source->module_scope);
    }

    scope_stack_pop_scope(names, &my global_scope);
    scope_stack_destroy(names);

    my errs->phase = ERROR_PHASE_UNKOWN;
    CHANGE_PHASE(PROGRAM_PHASE_RESOLVING_NAMES, PROGRAM_PHASE_NAMES_RESOLVED);
}

void program_typecheck(Program* me) {
    CHANGE_PHASE(PROGRAM_PHASE_NAMES_RESOLVED, PROGRAM_PHASE_TYPECHECKING);
    my errs->phase = ERROR_PHASE_TYPECHECK;
    set_error_config(&my errs->config);

    c_foreach(i, vec_source_file_ptr, my source_files_ordered) {
        SourceFile* source = *i.ref;
        if (!source->is_stdlib) {
            block_typecheck(&source->ast, (BlockTypecheckCtx){0}, my errs);
        }
    }

    clear_error_config();
    my errs->phase = ERROR_PHASE_UNKOWN;
    CHANGE_PHASE(PROGRAM_PHASE_TYPECHECKING, PROGRAM_PHASE_TYPECHECKED);
}

intptr_t program_get_file_count(Program const* me) {
    return hmap_str_box_source_file_size(&my source_files_map);
}
