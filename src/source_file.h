#pragma once

#include "block.h"
#include "scope.h"

typedef struct SourceDirective {
    enum {
        SOURCE_DIRECTIVE_REFERENCE_PATH,
        SOURCE_DIRECTIVE_REFERENCE_LIB,
        SOURCE_DIRECTIVE_REFERENCE_TYPES,
    } tag;
    cstr path;
    Span path_span;
    bool preserve;
} SourceDirective;

void source_directive_drop(SourceDirective* me);

#define i_key_class_names SourceDirective, source_directive
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct SourceFile {
    cstr canonical_path; // The canonical path of this file relative to the current working directory
    cstr contents;
    VecSourceDirective directives;
    Block ast;
    // If no errors should be generated from inside this file (for example for standard library files). If true this
    // file is not typechecked and duplicate identifiers are blamed on the other file exclusively.
    bool cant_have_errors;
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
