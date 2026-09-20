#pragma once

#include "block.h"
#include "scope.h"

typedef struct SourceFile SourceFile;

// A module named by a string somewhere in a file: import/export declarations, `import x = require("m")`,
// `import("m")` types and expressions, `declare module "m"`. The parser records them all (wherever they are), the
// program loads the files, the binder finds the module symbol (binder_module_of_ref())
typedef struct ModuleRef {
    cstr path; // Without quotes, unescaped
    Span span; // Of the string literal
    // `declare module "m" {}`: an ambient module (script) or an augmentation (module), not an import
    bool is_declaration;
    SourceFile* file; // What the program's module resolution found, may be null
    Symbol* module; // Memo of binder_module_of_ref()
    AliasState module_state;
} ModuleRef;

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
    ModuleRef** module_refs; // Owned, in source order
    u32 module_ref_count;
    u32 module_ref_cap;
    SourceFile* index_file; // The index(.d).ts file in the same directory as this file
} SourceFile;

SourceFile source_file_make(cstr canonical_path, cstr contents);
void source_file_drop(SourceFile* me);
// Takes path
ModuleRef* source_file_add_module_ref(SourceFile* me, cstr path, Span span);

// 0-based index of the line `pos` (which must point into the contents, or at their end) is on
unsigned source_file_line_index(SourceFile const* me, const char* pos);

static inline const char* source_file_start(SourceFile const* me) {
    return cstr_str(&my contents);
}
