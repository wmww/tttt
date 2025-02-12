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
    Scope scope;
    bool typecheck;
    cstr decl_override; // Used for the tests
    const char* display_path_override; // Used for the tests
    SourceFile* index_file; // The index(.d).ts file in the same directory as this file
} SourceFile;

SourceFile source_file_make(cstr canonical_path, cstr contents, ErrorCtx* errs);
SourceFile source_file_make_not_found_placeholder(ErrorCtx* errs);
void source_file_drop(SourceFile* me);

static inline const char* source_file_start(SourceFile const* me) {
    return cstr_str(&my contents);
}
