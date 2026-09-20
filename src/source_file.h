#pragma once

#include "block.h"
#include "scope.h"

typedef struct SourceFile {
    cstr canonical_path; // The canonical path of this file relative to the current working directory
    cstr contents;
    // Offset of the first character of each line, for span_line_index(). Diagnostics are sorted and printed by line,
    // counting newlines from the start of the file for each of them was a fifth of a run with 10k errors
    u32* line_starts;
    u32 line_count;
    Block ast;
    // If true this file is not typechecked and duplicate identifiers are blamed on the other file exclusively.
    bool is_stdlib;
    bool is_module;
    bool is_declaration;
    Scope* scope; // The file's scope, made when names are declared. A module's container is its module symbol
    cstr decl_override; // Used for the tests
    const char* display_path_override; // Used for the tests
    SourceFile* index_file; // The index(.d).ts file in the same directory as this file
} SourceFile;

SourceFile source_file_make(cstr canonical_path, cstr contents);
void source_file_drop(SourceFile* me);

// 0-based index of the line `pos` (which must point into the contents, or at their end) is on
unsigned source_file_line_index(SourceFile const* me, const char* pos);

static inline const char* source_file_start(SourceFile const* me) {
    return cstr_str(&my contents);
}
