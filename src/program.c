#include "program.h"
#include "parser.h"
#include "block.h"
#include "filesystem.h"
#include "error.h"
#include "statement.h"
#include "source_file.h"
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
    // TODO: move ErrorPhase to our header and rename to ProgramPhase
    ErrorPhase phase;
    ErrorCtx* errs; // unowned
    Filesystem const* fs;
    HmapStrBoxSourceFile source_files;
    VecSourceFilePtr pending_source_files; // files that have not been parsed yet
    SourceFile not_found_file; // used when a source file isn't found in the filesystem
} Program;

Program* program_new(ErrorCtx* errs, Filesystem const* fs_override) {
    return c_new(Program, {
        .phase = ERROR_PHASE_UNKOWN,
        .errs = errs,
        .fs = fs_override ? fs_override : &real_filesystem_quiet,
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
static SourceFile const* program_add_source(Program* me, const char* path) {
    cstr canonical_path;
    if (!filesystem_canonicalize_path(my fs, path, &canonical_path)) {
        return nullptr;
    }
    hmap_str_box_source_file_value const* value = hmap_str_box_source_file_get(
        &my source_files,
        cstr_sv(&canonical_path)
    );
    if (value) {
        cstr_drop(&canonical_path);
        return value->second.get;
    } else {
        cstr contents;
        if (!filesystem_read_entire_file(my fs, cstr_str(&canonical_path), &contents)) {
            cstr_drop(&canonical_path);
            return nullptr;
        }
        SourceFile* source_file = c_new(
            SourceFile,
            source_file_make(cstr_clone(canonical_path), contents, my errs)
        );
        hmap_str_box_source_file_insert(&my source_files, canonical_path, box_source_file_from_ptr(source_file));
        vec_source_file_ptr_push(&my pending_source_files, source_file);
        return source_file;
    }
}

SourceFile const* program_load(Program* me, const char* path) {
    ASSERT(my phase == ERROR_PHASE_UNKOWN);
    my phase = ERROR_PHASE_RESOLVE_NAMES;
    SourceFile const* main_source = program_add_source(me, path);
    if (!main_source) {
        error_ctx_error(my errs, span_null, 6053, "File '%s' not found.", path);
        return &my not_found_file;
    }
    while (vec_source_file_ptr_size(&my pending_source_files)) {
        VecSourceFilePtr pending_files = my pending_source_files;
        my pending_source_files = vec_source_file_ptr_init();
        c_foreach(i, vec_source_file_ptr, pending_files) {
            SourceFile* source = *i.ref;
            source->ast = parse_file(cstr_str(&source->contents), my errs);
            ErrorPhase prev_phase = error_ctx_swap_phase(my errs, ERROR_PHASE_RESOLVE_NAMES);
            name_stack_push_block_frame(source->name_stack);
            block_add_names(&source->ast, source->name_stack, my errs);
            error_ctx_swap_phase(my errs, prev_phase);
        }
        vec_source_file_ptr_drop(&pending_files);
    }
    ErrorPhase prev_phase = error_ctx_swap_phase(my errs, ERROR_PHASE_RESOLVE_NAMES);
    c_foreach(i, hmap_str_box_source_file, my source_files) {
        SourceFile* source = i.ref->second.get;
        block_resolve_names_only(&source->ast, source->name_stack, my errs);
        name_stack_pop_frame(source->name_stack);
    }
    error_ctx_swap_phase(my errs, prev_phase);
    my phase = ERROR_PHASE_UNKOWN;
    return main_source;
}

SourceFile const* program_queue_import(Program* me, Span path_span, const char* path) {
    ASSERT(my phase == ERROR_PHASE_RESOLVE_NAMES);
    SourceFile const* source = program_add_source(me, path);
    if (!source) {
        error_ctx_error(
            my errs,
            path_span,
            2307,
            "Cannot find module '%.*s' or its corresponding type declarations.",
            c_SPAN(path_span)
        );
        return &my not_found_file;
    }
    return source;
    return nullptr;
}

void program_typecheck(Program* me, bool show_errors) {
    ASSERT(my phase == ERROR_PHASE_UNKOWN);
    my phase = ERROR_PHASE_TYPECHECK;
    ErrorPhase prev_phase = error_ctx_swap_phase(my errs, ERROR_PHASE_TYPECHECK);
    c_foreach(i, hmap_str_box_source_file, my source_files) {
        SourceFile* source = i.ref->second.get;
        StatementCtx statement_ctx = statement_ctx_toplevel();
        block_typecheck(&source->ast, &statement_ctx, show_errors ? my errs : nullptr);
        statement_ctx_drop(&statement_ctx);
    }
    error_ctx_swap_phase(my errs, prev_phase);
    my phase = ERROR_PHASE_UNKOWN;
}
