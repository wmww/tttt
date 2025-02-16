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
    ASSERT(!errs->program);
    Program* ret = c_new(Program, {
        .errs = errs,
        .fs = fs,
        .global_scope = scope_init(errs),
    });
    errs->program = ret;
    program_add_builtins(ret);
    return ret;
}

static void program_drop(Program* me) {
    me->errs->program = nullptr;
    scope_drop(&my global_scope);
    hmap_str_box_source_file_drop(&my source_files_map);
    vec_source_file_ptr_drop(&my source_files_ordered);
}

void program_destroy(Program* me) {
    program_drop(me);
    free(me);
}

void program_add_globals_from_scope(Program* me, Scope const* scope) {
    scope_copy_declarations_from(&my global_scope, scope);
}

void program_add_globals_from_program(Program* me, Program const* other) {
    program_add_globals_from_scope(me, &other->global_scope);
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

    parse_file(source_file, my errs);

    hmap_str_box_source_file_insert(&my source_files_map, canonical_path, box_source_file_from_ptr(source_file));

    c_foreach(j, vec_source_directive, source_file->directives) {
        switch (j.ref->tag) {
            case SOURCE_DIRECTIVE_REFERENCE_PATH:
            case SOURCE_DIRECTIVE_REFERENCE_LIB:
            case SOURCE_DIRECTIVE_REFERENCE_TYPES:
                program_load_import_idempotent(me, j.ref->path_span, cstr_sv(&j.ref->path));
                break;
        }
    }

    c_foreach(i, vec_statement, source_file->ast.statements) {
        if (i.ref->tag == STATEMENT_IMPORT_EXPORT) {
            PortStatement* port = i.ref->import_export;
            SourceFile* imported = nullptr;
            if (!port->requires_target) {
                imported = source_file;
            } else if (span_valid(port->target_path_span)) {
                imported = program_load_import_idempotent(me, port->target_path_span, cstr_sv(&port->target_path));
            }
            port_statement_assign_source(port, imported);
        }
    }

    // Add here so imports come before importing file
    vec_source_file_ptr_push(&my source_files_ordered, source_file);

    return source_file;
}

SourceFile* program_load(Program* me, csview path) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my errs->phase = ERROR_PHASE_LOAD;
    set_error_config(&my errs->config);

    // Add the source file to the program
    SourceFile* main_source = program_add_source_idempotent(me, path);
    if (!main_source) {
        error_ctx_error(my errs, span_null, 6053, "File '%.*s' not found.", c_SV(path));
        // Continue with null
    }

    clear_error_config();
    my errs->phase = ERROR_PHASE_UNKOWN;
    return main_source;
}

SourceFile* program_preload(Program* me, csview path) {
    SourceFile* source = program_load(me, path);
    if (source) {
        source->cant_have_errors = true;
    }
    return source;
}

SourceFile* program_preload_es5(Program* me) {
    return program_preload(me, c_sv("thirdparty/lib/lib.es5.d.ts"));
}

void program_resolve_names(Program* me) {
    CHANGE_PHASE(PROGRAM_PHASE_LOADING, PROGRAM_PHASE_RESOLVING_NAMES);
    my errs->phase = ERROR_PHASE_RESOLVE_NAMES;

    c_foreach(i, vec_source_file_ptr, my source_files_ordered) {
        SourceFile* source = *i.ref;
        block_add_names(&source->ast, source_file_get_scope_or(source, &my global_scope));
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
        if (!source->cant_have_errors) {
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
