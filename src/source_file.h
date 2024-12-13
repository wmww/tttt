#pragma once

#include "block.h"
#include "scope.h"

typedef struct SourceFile {
    cstr canonical_path; // The canonical path of this file relative to the current working directory
    cstr contents;
    Block ast;
    Scope scope;
    bool typecheck;
    cstr decl_override; // Used for the tests
    const char* display_path_override; // Used for the tests
    SourceFile* index_file; // The index(.d).ts file in the same directory as this file
} SourceFile;

SourceFile source_file_make(cstr canonical_path, cstr contents, ErrorCtx* errs);
void source_file_drop(SourceFile* me);

static inline const char* source_file_start(SourceFile const* me) {
    return cstr_str(&my contents);
}
