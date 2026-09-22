#pragma once

#include "block.h"
#include "scope.h"

typedef struct SourceFile SourceFile;

// A module named by a string somewhere in a file: import/export declarations, `import x = require("m")`,
// `import("m")` types and expressions, `declare module "m"`. The parser records them all (wherever they are), the
// program loads the files, the binder finds the module symbol (binder_module_of_ref())
// How a module was named, which under node16/nodenext decides the conditions its package's `exports` are read with
typedef enum ModuleRefSyntax {
    MODULE_REF_IMPORT, // import/export declarations and import types: the format of the file
    MODULE_REF_REQUIRE, // `import x = require("m")`: always CommonJS
    MODULE_REF_DYNAMIC, // `import("m")`: always ESM
} ModuleRefSyntax;

// Why a ModuleRef has no file, when it isn't simply that nothing was found
typedef enum ModuleRefFailure {
    MODULE_REF_NOT_FOUND,
    MODULE_REF_IS_JS, // Resolved to JavaScript, without allowJs: `any`, TS7016 under noImplicitAny
    MODULE_REF_IS_JSON, // Resolved to a .json file: `any` for now
    MODULE_REF_JSON_WITHOUT_OPTION, // TS2732
    MODULE_REF_ARBITRARY_EXTENSION, // Resolved to a .d.*.ts file without allowArbitraryExtensions: TS6263
    MODULE_REF_JSX_NOT_SET, // Resolved to a .tsx/.jsx file without `jsx`: TS6142
    MODULE_REF_NEEDS_EXTENSION, // TS2834 / TS2835 (with `suggestion`)
} ModuleRefFailure;

typedef struct ModuleRef {
    cstr path; // Without quotes, unescaped
    Span span; // Of the string literal
    // `declare module "m" {}`: an ambient module (script) or an augmentation (module), not an import
    bool is_declaration;
    ModuleRefSyntax syntax;
    SourceFile* file; // What the program's module resolution found, may be null
    ModuleRefFailure failure; // Without a file
    bool used_ts_extension; // The name ends in .ts/.tsx/.mts/.cts and that is the file: TS5097 without the option
    cstr resolved_path; // MODULE_REF_IS_JS, MODULE_REF_ARBITRARY_EXTENSION: the file, as messages name it. MODULE_REF_NEEDS_EXTENSION: the specifier tsc suggests, if any
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
    // A .js/.jsx/.mjs/.cjs file: only part of a program with `allowJs`
    bool is_js;
    // Semantic diagnostics (names, types) inside this file aren't reported: JavaScript without `checkJs` or a leading
    // `// @ts-check`, any file with `// @ts-nocheck`
    bool unchecked;
    // With node16/node18/nodenext modules or resolution: whether the file is an ES module, the package.json that has a
    // say in that (as messages name it, empty if there is none) and whether it says (src/module_resolution.h)
    bool is_esm;
    bool package_has_type;
    cstr package_json;
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

// How messages name the file: the way the test wrote it under the harness, else the canonical path
static inline csview source_file_display_path(SourceFile const* me) {
    return my display_path_override ? csview_from(my display_path_override) : cstr_sv(&my canonical_path);
}

// What there is to say about a module name that did find its file (TS5097)
void module_ref_report_resolved(ModuleRef const* me, struct ErrorCtx* errs);

// The diagnostic for a module name that led to no module (neither a file nor an ambient module): TS2307 and its
// flavours, by what module resolution found instead
void module_ref_report_unresolved(ModuleRef const* me, struct ErrorCtx* errs);

// 0-based index of the line `pos` (which must point into the contents, or at their end) is on
unsigned source_file_line_index(SourceFile const* me, const char* pos);

static inline const char* source_file_start(SourceFile const* me) {
    return cstr_str(&my contents);
}
