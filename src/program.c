#include "program.h"
#include "parser.h"
#include "block.h"
#include "filesystem.h"
#include "error.h"
#include "statement.h"
#include "source_file.h"
#include "import_statement.h"
#include "name_stack.h"

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

typedef struct Program {
    ErrorCtx* errs; // unowned
    Fs const* fs;
    HmapStrBoxSourceFile source_files; // All loaded source files. May or may not be parsed or have names resolved yet.
                                       // Keys are absolute paths.
    VecSourceFilePtr pending_source_files; // files that have not been parsed yet
    SourceFile not_found_file; // used when a source file isn't found in the filesystem
} Program;

Program* program_new(ErrorCtx* errs, Fs const* fs) {
    ASSERT(errs);
    ASSERT(fs);
    return c_new(Program, {
        .errs = errs,
        .fs = fs,
        .not_found_file = source_file_make(cstr_null, cstr_null, errs),
    });
}

static void program_drop(Program* me) {
    hmap_str_box_source_file_drop(&my source_files);
    vec_source_file_ptr_drop(&my pending_source_files);
    source_file_drop(&my not_found_file);
}

void program_destroy(Program* me) {
    program_drop(me);
    free(me);
}

void program_preload(Program* me, const char* path) {
    ignore me;
    ignore path;
    NONFATAL("Preloading not yet implemented");
}

// Returns null if path can't be loaded
static SourceFile* program_add_source(Program* me, const char* path) {
    cstr absolute_path;
    absolute_path = fs_absolute_path_of(my fs, path);
    auto value = hmap_str_box_source_file_get(&my source_files, cstr_sv(&absolute_path));
    if (value) {
        // A source file is already created for this path
        cstr_drop(&absolute_path);
        return value->second.get;
    } else {
        // No source file is created for this path, load it
        cstr contents;
        if (!fs_read_entire_file(my fs, cstr_str(&absolute_path), &contents)) {
            cstr_drop(&absolute_path);
            return nullptr;
        }
        SourceFile* source_file = c_new(
            SourceFile,
            source_file_make(cstr_clone(absolute_path), contents, my errs)
        );
        hmap_str_box_source_file_insert(&my source_files, absolute_path, box_source_file_from_ptr(source_file));
        vec_source_file_ptr_push(&my pending_source_files, source_file);
        return source_file;
    }
}

static void program_handle_import(Program* me, ImportStatement* import) {
    cstr start_path = cstr_from_sv(dirname_of(cstr_str(&import->import_keyword_span.source->absolute_path)));
    cstr source_path = absolute_path_of(cstr_str(&start_path), cstr_str(&import->path));
    cstr_drop(&start_path);
    if (fs_is_directory(my fs, cstr_str(&source_path))) {
        cstr_append_lit(&source_path, "/index.ts");
    } else {
        cstr_append_lit(&source_path, ".ts");
    }
    // TODO: handle js
    SourceFile* source = program_add_source(me, cstr_str(&source_path));
    cstr_drop(&source_path);

    if (source) {
        import->source_file = source;
    } else {
        error_ctx_error(
            my errs,
            import->path_span,
            2307,
            "Cannot find module '%s' or its corresponding type declarations.",
            cstr_str(&import->path)
        );
        import->source_file = &my not_found_file;
    }
}

SourceFile const* program_load(Program* me, const char* path) {
    // This function should only be called by our owner, not indirectly by ourselves
    ASSERT(my errs->phase == ERROR_PHASE_UNKOWN);
    my errs->phase = ERROR_PHASE_LOAD;
    set_error_config(&my errs->config);

    // Add the source file to the program
    SourceFile const* main_source = program_add_source(me, path);
    if (!main_source) {
        error_ctx_error(my errs, span_null, 6053, "File '%s' not found.", path);
        my errs->phase = ERROR_PHASE_UNKOWN;
        return &my not_found_file;
    }

    my errs->phase = ERROR_PHASE_RESOLVE_NAMES;
    VecSourceFilePtr in_flight_source_files = vec_source_file_ptr_init();

    // There will initially be one pending source file (the one we just added). In each iteration more pending source
    // files may be added due to imports.
    while (vec_source_file_ptr_size(&my pending_source_files)) {
        // Remove the top source file from the pending list and parse it (order of processing shouldn't matter here)
        SourceFile* source = vec_source_file_ptr_pull(&my pending_source_files);
        parse_file(source, my errs);

        // Load imported files
        c_foreach(i, vec_statement, source->ast.statements) {
            if (i.ref->tag == STATEMENT_IMPORT) {
                program_handle_import(me, i.ref->import);
            }
        }

        // Add the toplevel block's names to the file's scope
        block_add_names(&source->ast, &source->scope);

        // Add it to the list to process below
        vec_source_file_ptr_push(&in_flight_source_files, source);
    }

    c_foreach(i, vec_source_file_ptr, in_flight_source_files) {
        SourceFile* source = *i.ref;
        scope_process_imports(&source->scope);
    }

    NameStack* names = name_stack_new();

    c_foreach(i, vec_source_file_ptr, in_flight_source_files) {
        SourceFile* source = *i.ref;
        scope_push_to_stack(&source->scope, names);
        block_lookup_names(&source->ast, names, my errs);
        scope_pop_from_stack(&source->scope);
    }

    name_stack_destroy(names);

    clear_error_config();
    vec_source_file_ptr_drop(&in_flight_source_files);
    my errs->phase = ERROR_PHASE_UNKOWN;
    return main_source;
}

void program_typecheck(Program* me) {
    ASSERT(my errs->phase == ERROR_PHASE_UNKOWN);
    my errs->phase = ERROR_PHASE_TYPECHECK;
    set_error_config(&my errs->config);

    c_foreach(i, hmap_str_box_source_file, my source_files) {
        SourceFile* source = i.ref->second.get;
        StatementCtx statement_ctx = statement_ctx_toplevel();
        block_typecheck(&source->ast, &statement_ctx, my errs);
        statement_ctx_drop(&statement_ctx);
    }

    clear_error_config();
    my errs->phase = ERROR_PHASE_UNKOWN;
}

intptr_t program_get_file_count(Program const* me) {
    return hmap_str_box_source_file_size(&my source_files);
}
