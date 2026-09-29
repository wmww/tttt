#pragma once

#include "block.h"
#include "scope.h"
#include "compiler_options.h"

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
    MODULE_REF_IS_JSON, // Resolved to a .json file that isn't loaded
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
    // Never resolved to a file, only an ambient module (or pattern) can be it: the implicit jsx-runtime import of a script
    bool ambient_only;
    ModuleRefSyntax syntax;
    // ImportResolutionOverride (import_attributes.h): `resolution-mode` of a type-only import or an import type
    uint8_t resolution_override;
    SourceFile* file; // What the program's module resolution found, may be null
    ModuleRefFailure failure; // Without a file
    bool used_ts_extension; // The name ends in .ts/.tsx/.mts/.cts and that is the file: TS5097 without the option
    // MODULE_REF_IS_JS in a package (tsc's packageId): what TS7016 suggests (createModuleNotFoundChain), set once the
    // program is loaded
    enum {
        MODULE_REF_HINT_NONE,
        MODULE_REF_HINT_INSTALL_TYPES, // `npm i --save-dev @types/x`
        MODULE_REF_HINT_TYPES_EXIST, // @types/x is in the program: a pull request to DefinitelyTyped
        MODULE_REF_HINT_BUNDLES_TYPES, // The package has declaration files: `declare module`
    } package_hint;
    cstr package_name;
    // tsc's alternateResult, as messages name it: the declarations another resolution would find (TS2307, TS7016)
    cstr alternate_result;
    cstr resolved_path; // MODULE_REF_IS_JS, MODULE_REF_ARBITRARY_EXTENSION: the file, as messages name it. MODULE_REF_NEEDS_EXTENSION: the specifier tsc suggests, if any
    Symbol* module; // Memo of binder_module_of_ref()
    bool jsx_runtime_reported; // SourceFile.jsx_runtime_ref: that it can't be found was said (once per file, like tsc)
    AliasState module_state;
} ModuleRef;

// A `// @ts-expect-error` or `// @ts-ignore` comment (tsc's CommentDirective): the diagnostics that start on the next
// line (blank lines and `//` comment lines in between don't count) are not reported. An unused @ts-expect-error is
// TS2578. See program_apply_comment_directives()
typedef struct CommentDirective {
    const char* start; // tsc's range: the whole `//` comment, the last line of a `/* */` comment
    const char* end;
    u32 line; // The line the comment ends on, which is what the directive is looked up by
    bool expect_error;
    bool used;
} CommentDirective;

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
    // First loaded because an import (not a module augmentation, not a root or `/// <reference path>`) resolved to it:
    // tsc names such a file by its absolute path, which the harness writes before it strips its `/.src/` (it shows in
    // the types baseline's underlines of `typeof import("m")`)
    bool loaded_by_import;
    // A JavaScript file of a package the program followed imports into (maxNodeModuleJsDepth): how many such files deep
    // (tsc's currentNodeModulesDepth), 0 for the project's own. Such a file is never checked (isSourceFileFromExternalLibrary)
    u32 node_modules_js_depth;
    u32 program_order; // Its index in the program's files (tsc's host.getSourceFiles()), set once loaded
    bool is_module;
    // The first statement that makes this file a module (tsc's externalModuleIndicator, as getErrorSpanForNode spans it:
    // a declaration's name, else the statement). Invalid if none does
    Span module_indicator;
    bool is_declaration;
    // The parser reported something (tsc's hasParseDiagnostics): grammar errors aren't reported then, see
    // error_ctx_grammar_error_d()
    bool has_parse_errors;
    // A .js/.jsx/.mjs/.cjs file: only part of a program with `allowJs`
    bool is_js;
    // A .json file an import resolved to (resolveJsonModule): one expression, which is what the module is (`export =`)
    bool is_json;
    // tsc's LanguageVariant.JSX (.tsx and JavaScript files): `<` at the start of an expression is a JSX element, never
    // a type assertion
    bool is_jsx;
    // The parser saw `import.meta` / a JSX element or fragment (what moduleDetection `auto` looks for besides imports and
    // exports: tsc's isFileProbablyExternalModule, isFileModuleFromUsingJSXTag)
    bool has_import_meta;
    bool has_jsx;
    // A JavaScript file with a `require()` call or an assignment to `exports.x`/`module.exports` (tsc's
    // commonJsModuleIndicator): a module, unless it has ES imports or exports (see is_commonjs)
    bool has_commonjs;
    // A JavaScript file that is a CommonJS module: `exports.x =` and `module.exports =` declare its exports, `module`,
    // `exports` and `require` mean something
    bool is_commonjs;
    Span module_exports_span; // The `module` of the first `module.exports` of a JavaScript file (where tsc declares it)
    // The JSX pragmas of the comments at the top of the file (tsc's `@jsx`, `@jsxFrag`, `@jsxRuntime` and
    // `@jsxImportSource` in `/* */` comments): their argument, empty if there is none. The first `@jsx`/`@jsxFrag`
    // counts, the last `@jsxRuntime`/`@jsxImportSource` (like tsc's uses of file.pragmas)
    struct {
        csview factory, fragment_factory, runtime, import_source;
    } jsx_pragmas;
    bool jsx_fragment_factory_looked_up; // By the resolve walk, for TS2879: once per file, like tsc's getJSXFragmentType
    // Semantic diagnostics (names, types) inside this file aren't reported: JavaScript without `checkJs` or a leading
    // `// @ts-check`, any file with `// @ts-nocheck`
    bool unchecked;
    // Unchecked, but checked anyway for a few errors (tsc's isPlainJsFile: JavaScript without a `@ts-check`/`@ts-nocheck`
    // and with checkJs not set at all): only the ones on tsc's plainJSErrors list are reported
    bool plain_js;
    // With node16/node18/nodenext modules or resolution, or in node_modules: whether the file is an ES module, the
    // package.json that has a say in that (as messages name it, empty if there is none) and whether it says
    // (src/module_resolution.h)
    bool is_esm;
    bool has_format; // Whether is_esm means anything: tsc's impliedNodeFormat isn't undefined
    bool package_has_type;
    cstr package_json;
    Scope* scope; // The file's scope, made when names are declared. A module's container is its module symbol
    cstr decl_override; // Used for the tests
    const char* display_path_override; // Used for the tests
    ModuleRef** module_refs; // Owned, in source order
    u32 module_ref_count;
    u32 module_ref_cap;
    // With `importHelpers`, a module that isn't a declaration file imports "tslib" implicitly (tsc's
    // externalHelpersModuleReference). The first of module_refs, with an empty span at the start of the file
    ModuleRef* helpers_ref; // (the first of module_refs)
    // The factory's module with react-jsx or pragmas (tsc's getJSXRuntimeImport: `react/jsx-runtime`), null if the file
    // doesn't import one. One of module_refs (after helpers_ref), resolved like an import in a module or a JavaScript
    // file (tsc's collectExternalModuleReferences), `ambient_only` in a script
    ModuleRef* jsx_runtime_ref;
    SourceFile* index_file; // The index(.d).ts file in the same directory as this file
    CommentDirective* comment_directives; // Owned, sorted by position (and so by line), recorded by the parser
    u32 comment_directive_count;
    u32 comment_directive_cap;
    // Declarations that may be unused (noUnusedLocals/noUnusedParameters), registered by the resolve walk, see unused.h
    struct UnusedCandidate* unused; // Owned
    u32 unused_count;
    u32 unused_cap;
    bool escaped_names; // names_register_source() registered it
} SourceFile;

SourceFile source_file_make(cstr canonical_path, cstr contents);
// The module format the file is emitted in (tsc's getEmitModuleFormatOfFile): with node16/nodenext its implied format,
// else a .cts/.mts extension or an explicit package.json `type` decide, else the `module` option
ModuleKind source_file_emit_module_kind(SourceFile const* me);
// tsc's getImpliedNodeFormatForEmit: MODULE_KIND_COMMONJS, MODULE_KIND_ESNEXT or MODULE_KIND_NONE (undefined: the module
// option decides)
ModuleKind source_file_implied_format_for_emit(SourceFile const* me);
void source_file_drop(SourceFile* me);
// Takes path
ModuleRef* source_file_add_module_ref(SourceFile* me, cstr path, Span span);
// tsc's getJSXRuntimeImport(getJSXImplicitImportBase()): `react/jsx-runtime` (react-jsx), `preact/jsx-dev-runtime`
// (react-jsxdev with `@jsxImportSource preact`), null if the file's JSX doesn't import its factory
cstr source_file_jsx_runtime_import(SourceFile const* me);

// Records a directive comment the parser came across (again, possibly: backtracking parses hop comments repeatedly)
void source_file_add_comment_directive(SourceFile* me, const char* start, const char* end, bool expect_error);
// The directive that the comment ending on `line` is, if any. Two on one line: the last one, like tsc's map by line
CommentDirective* source_file_comment_directive_at_line(SourceFile* me, u32 line);

// Whether the checker walks the file: a checked one, or a plain JavaScript one for the errors that are reported there
static inline bool source_file_is_checked(SourceFile const* me) {
    return !my unchecked || my plain_js;
}

// How messages name the file: the way the test wrote it under the harness, else the canonical path
static inline csview source_file_display_path(SourceFile const* me) {
    return my display_path_override ? csview_from(my display_path_override) : cstr_sv(&my canonical_path);
}

// What there is to say about a module name that did find its file (TS5097)
// type_only: of an `import type`/`export type` declaration (no TS2846 for naming a .d.ts)
void module_ref_report_resolved(ModuleRef const* me, bool type_only, struct ErrorCtx* errs);

// The diagnostic for a module name that led to no module (neither a file nor an ambient module): TS2307 and its
// flavours, by what module resolution found instead
void module_ref_report_unresolved(ModuleRef const* me, struct ErrorCtx* errs);

// 0-based index of the line `pos` (which must point into the contents, or at their end) is on
unsigned source_file_line_index(SourceFile const* me, const char* pos);

static inline const char* source_file_start(SourceFile const* me) {
    return cstr_str(&my contents);
}
