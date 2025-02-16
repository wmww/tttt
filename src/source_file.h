#pragma once

#include "block.h"
#include "scope.h"

typedef struct SourceFile {
    cstr canonical_path; // The canonical path of this file relative to the current working directory
    cstr contents;
    Block ast;
    // If true this file is not typechecked and duplicate identifiers are blamed on the other file exclusively.
    bool is_standard_library;
    bool is_module;
    Scope module_scope; // Only used if is_module is true
    VecScopeCPtr global_scopes; // Toplevel global augmenttion scopes
    cstr decl_override; // Used for the tests
    const char* display_path_override; // Used for the tests
    SourceFile* index_file; // The index(.d).ts file in the same directory as this file
} SourceFile;

SourceFile source_file_make(cstr canonical_path, cstr contents);
void source_file_drop(SourceFile* me);
void source_file_set_module_true(SourceFile* me, ErrorCtx* errs);

static inline const char* source_file_start(SourceFile const* me) {
    return cstr_str(&my contents);
}

static inline Scope* source_file_get_scope_if_module(SourceFile* me) {
    return my is_module ? &my module_scope : nullptr;
}

static inline Scope* source_file_get_scope_or(SourceFile* me, Scope* fallback) {
    return my is_module ? &my module_scope : fallback;
}
