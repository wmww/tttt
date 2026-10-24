#pragma once

#include "ast_alloc.h"
#include "block.h"
#include "scope.h"
#include "compiler_options.h"
#include "error.h"
#include "flow.h"
#include <pthread.h>

// An assignment or call of a CommonJS file's statement (its index) that may declare an export (SourceFile)
typedef struct CommonJsCandidate {
    struct Expr const* expr;
    u32 statement;
    bool top_level; // The statement's own expression, or an assignment chain's or a comma's
} CommonJsCandidate;

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
    // Resolved to a referenced project's source whose output declaration file (unbuilt_output) doesn't exist, under -p:
    // TS6305, the module is `any`
    MODULE_REF_NOT_BUILT,
} ModuleRefFailure;

typedef struct ModuleRef {
    cstr path; // Without quotes, unescaped
    Span span; // Of the string literal
    // `declare module "m" {}`: an ambient module (script) or an augmentation (module), not an import
    bool is_declaration;
    // `module "a" {}` directly in `declare module "D" {}` of a script: an augmentation of the file "a" resolves to
    // (tsc's isModuleAugmentationExternal), so it is resolved like an import
    bool nested_in_ambient_module;
    // Never resolved to a file, only an ambient module (or pattern) can be it: the implicit jsx-runtime import of a script
    bool ambient_only;
    // An import or export declaration's in an ambient module's or `global {}`'s body. In a module those are
    // augmentations, whose bodies tsc's collectModuleReferences doesn't walk: the name resolves only through an import
    // of the file that is collected, same name and mode (tsc's getResolvedModule), else it's ambient_only
    // (program_mark_uncollected_refs())
    bool in_augmentation_body;
    // Resolved by tsc's initializeTypeChecker before the scripts' ambient modules are merged into the globals: never an
    // ambient module of its name (program_mark_init_resolved_imports())
    bool resolved_before_ambient;
    ModuleRefSyntax syntax;
    // ImportResolutionOverride (import_attributes.h): `resolution-mode` of a type-only import or an import type
    uint8_t resolution_override;
    bool type_only_import; // Of an `import type` declaration (TS1541 rather than TS1479)
    bool import_type; // Of an import type `import("m")` (TS1542 rather than TS1479)
    bool require_call; // A JavaScript file's `require("m")` call
    SourceFile* file; // What the program's module resolution found, may be null
    ModuleRefFailure failure; // Without a file
    bool used_ts_extension; // The name ends in .ts/.tsx/.mts/.cts and that is the file: TS5097 without the option
    // The file is another project's whose directories aren't placed like this one's: rewriting a `.ts` import path is
    // unsafe (TS2878, program_rewrite_crosses_projects())
    bool rewrite_crosses_projects;
    bool external_library; // Found by a node_modules lookup (tsc's isExternalLibraryImport)
    // `with { type: "css" }` of the import, export or import type (owned by it), and `import("m", options)`'s call:
    // which pattern ambient modules it can be (binder_find_module(), tsgo's tryResolvePatternAmbientModule)
    // (A copy: the AST it was read off may be a speculative parse's, freed since. Owned, null for none)
    struct ModuleRefAttrs* attrs;
    struct ModuleRefRare* rare; // Owned, null when all of it is: read through module_ref_rare()
    Symbol* module; // Memo of binder_module_of_ref()
    Symbol* detached_module; // An augmentation of nothing: what its declarations merge into (augmented_module())
    AliasState module_state;
} ModuleRef;

// What only failed, redirected or augmenting module references have (written through module_ref_rare_mut())
typedef struct ModuleRefRare {
    // A module augmentation's: the file its name resolves to, which it doesn't add to the program (tsc's
    // processImportedModules only loads `file.imports`). `file` is set from it once loading is over
    cstr augmented_path;
    // The second copy of a package (same name and version) that `file`, the first, stands in for: messages name it
    // (tsc's redirect source file keeps its own fileName)
    SourceFile* redirected_from;
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
    cstr resolved_path; // MODULE_REF_IS_JS, MODULE_REF_ARBITRARY_EXTENSION: the file, as messages name it. MODULE_REF_NEEDS_EXTENSION: the specifier tsc suggests, if any. MODULE_REF_NOT_BUILT: the source, absolute
    cstr unbuilt_output; // MODULE_REF_NOT_BUILT: the output declaration file, absolute
} ModuleRefRare;

extern const ModuleRefRare module_ref_no_rare;
static inline ModuleRefRare const* module_ref_rare(ModuleRef const* me) {
    return me->rare ? me->rare : &module_ref_no_rare;
}
// Allocates it if needed
ModuleRefRare* module_ref_rare_mut(ModuleRef* me);
// An import call, import type or `require()` call: tsc's file.imports lists them after the declarations
static inline bool module_ref_collected_late(ModuleRef const* me) {
    return me->import_type || me->require_call || me->syntax == MODULE_REF_DYNAMIC;
}

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

typedef enum IncludeKind : uint8_t {
    INCLUDE_NONE = 0,
    INCLUDE_ROOT, // A root file (the command line, a config's files or include)
    INCLUDE_IMPORT, // A module name (an import, export, require, import type...)
    INCLUDE_REFERENCE, // `/// <reference path>`
} IncludeKind;

typedef struct IncludeReason {
    IncludeKind kind;
    Span at; // INCLUDE_IMPORT: the module name literal; INCLUDE_REFERENCE: the path
    cstr package_id; // INCLUDE_IMPORT of a package's file: its name, path in it and version (tsc's packageId)
} IncludeReason;

typedef struct SourceFile {
    // Its index in the process-wide file table (spans hold it, source_file_register()): first, see source_file_id()
    u32 id;
    cstr canonical_path; // The canonical path of this file relative to the current working directory
    cstr contents;
    // Whether contents holds "@link" anywhere: 0 not asked yet, 1 no, 2 yes (source_file_has_link_tag(), benign race: idempotent)
    uint8_t has_link_tag;
    // Not 0: contents is a private read-only mapping of the file (source_text_map()), unmapped by source_file_drop()
    u32 text_mapping;
    // Offset of the first character of each line, for span_line_index(). Diagnostics are sorted and printed by line,
    // counting newlines from the start of the file for each of them was a fifth of a run with 10k errors
    u32* line_starts;
    u32 line_count;
    Block ast;
    AstArena arena; // What the parser allocated for `ast` (ast_alloc.h)
    // If true this file is not typechecked and duplicate identifiers are blamed on the other file exclusively.
    bool is_stdlib;
    // First loaded because an import (not a module augmentation, not a root or `/// <reference path>`) resolved to it
    // (a package's file found so gets no declaration diagnostics: tsc's isSourceFileFromExternalLibrary)
    bool loaded_by_import;
    bool is_root; // program_load() loaded it: a root file of the program (tsc's rootNames)
    // The first module name or `/// <reference path>` that refers to the file (a root file's too: tsc's first referenced-
    // file reason), where tsc's diagnostics explaining a file go (TS6307, TS6059); invalid for a root file nothing
    // refers to, which gets a global one
    Span included_at;
    // Every reason the file is in the program, in tsc's order (its fileIncludeReasons: the loader's depth-first walk,
    // every edge), for the chains of diagnostics explaining the file (program_error_explaining_file()). malloc'd
    bool module_indicators_scanned; // is_module and module_indicator from the statements (program.c)
    struct IncludeReason* include_reasons;
    u32 include_reason_count;
    u32 include_reason_cap;
    // A JavaScript file of a package the program followed imports into (maxNodeModuleJsDepth): how many such files deep
    // (tsc's currentNodeModulesDepth), 0 for the project's own. Such a file is never checked (isSourceFileFromExternalLibrary)
    u32 node_modules_js_depth;
    // First reached through a node_modules lookup (an import of a package, also one that links to a workspace's
    // sources), or from such a file, and never from the program's own files: tsc's sourceFilesFoundSearchingNodeModules,
    // isSourceFileFromExternalLibrary. Never emitted (no rootDir or composite file list checks)
    bool found_searching_node_modules;
    u32 program_order; // Its index in the program's files (tsc's host.getSourceFiles()), set once loaded
    bool is_module;
    // The first statement that makes this file a module (tsc's externalModuleIndicator, as getErrorSpanForNode spans it:
    // a declaration's name, else the statement). Invalid if none does
    Span module_indicator;
    bool is_declaration;
    // The parser reported something (tsc's hasParseDiagnostics): grammar errors aren't reported then, see
    // error_ctx_grammar_error_d()
    bool has_parse_errors;
    // Where those parse errors start (in no order), for tsc's containsParseError of a node (a node whose parsing
    // reported one): source_file_parse_error_within()
    char const** parse_error_starts;
    u32 parse_error_count;
    // A .js/.jsx/.mjs/.cjs file: only part of a program with `allowJs`
    bool is_js;
    // Its resolve walk reads what another file's walk writes, so it runs after the parallel ones (plan 45,
    // binder_mark_serial_resolves())
    bool resolve_serial;
    // A .json file an import resolved to (resolveJsonModule): one expression, which is what the module is (`export =`)
    bool is_json;
    // The tsconfig.json/jsconfig.json the options come from (its diagnostics sort by its path like any file's: tsgo's
    // config file has a FileName)
    bool is_config;
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
    // A file with has_commonjs: the assignments and calls that may declare a CommonJS export, in walk order, collected
    // right after parsing (by the load pool's thread: source_collect_commonjs_candidates()) so the serial declare step
    // doesn't walk the whole tree (scope_add_commonjs_module())
    CommonJsCandidate* commonjs_candidates;
    u32 commonjs_candidate_count;
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
    // A second copy of a package (same name and version) whose imports mean the first: tsc's redirect source file has
    // the first copy's AST, so nothing in this one's text is reported (not even parse errors), nor bound
    bool package_redirect;
    // Loaded, but not one of the program's files (program_collect_files()): a redirect, or a file only reached through
    // a redirect's imports (tsgo's collectFiles never visits it). Still findable by path, a redirect as itself
    bool excluded;
    struct SourceFile* package_redirect_target; // The first copy, what an augmentation of this one augments
    // The package id of the first import (a TypeScript resolution) that reached the file, the id tsgo's parse task data
    // gets; empty if none had one. What program_collect_files() deduplicates by
    cstr package_id;
    // Every file the loader reached from this one (imports, references, type references, libraries...), in the order
    // it did, repeats included: tsgo's parse subtasks, which program_collect_files() walks. malloc'd, freed by it
    struct SourceFile** load_children;
    u32 load_child_count;
    u32 load_child_cap;
    bool collected; // program_collect_files()'s walk visited it
    // 1 + the index of the referenced project (project.h) whose options resolve the file's module names (tsc's
    // getRedirectReferenceForResolution): the project an output declaration file is of
    u32 resolution_project;
    // A TypeScript file (not TSX, not a declaration file) whose outermost function bodies the parser skips where that is
    // safe, to parse and walk when a checker first needs them (plan 78, program_set_defer_bodies())
    bool defer_bodies;
    // What the file's resolve walk deferred, null if nothing (program_materialize_file()). Owned
    struct DeferredBodies* deferred_bodies;
    bool has_deferred_bodies; // The parser deferred some (perhaps parsing it later all the same: an IIFE's)
    // A referenced project's output declaration file whose project emits ES modules (`module` es2015..esnext): an ES module's import of it
    // has no synthetic default (tsc's canHaveSyntheticDefault, getRedirectFromSourceFile)
    bool reference_emits_esm;
    // A referenced project's built output declaration file whose project preserves const enums: they aren't ambient
    // (tsc's shouldPreserveConstEnums of getRedirectFromOutput, TS2748)
    bool reference_preserves_const_enums;
    // Unchecked, but checked anyway for a few errors (tsc's isPlainJsFile: JavaScript without a `@ts-check`/`@ts-nocheck`
    // and with checkJs not set at all): only the ones on tsc's plainJSErrors list are reported
    bool plain_js;
    // A narrowing flow node of the file names something that wasn't resolved when it was made: every import may be
    // narrowed (Symbol.flow_narrowed can't say)
    atomic_bool flow_unresolved_names;
    // With node16/node18/nodenext modules or resolution, or in node_modules: whether the file is an ES module, the
    // package.json that has a say in that (as messages name it, empty if there is none) and whether it says
    // (src/module_resolution.h)
    bool is_esm;
    bool has_format; // Whether is_esm means anything: tsc's impliedNodeFormat isn't undefined
    bool package_has_type;
    bool package_type_decides; // See FileFormat.package_type_decides
    cstr package_json;
    Scope* scope; // The file's scope, made when names are declared. A module's container is its module symbol
    // JavaScript `{}` initializers with expandos (`var o = {}; o.x = 1`) and the symbol they are the value of: the
    // literal's type is that container's (tsgo: the expandos are the literal's symbol's), Expr.expando_container.
    // One entry per literal and binder (a program that binds the file again has its own host, the checker picks its
    // binder's). malloc'd, written by the file's resolve walk: two programs must not bind the same file at once
    struct ExpandoContainer { struct Expr const* literal; Symbol const* host; }* expando_containers;
    u32 expando_container_count;
    cstr decl_override; // Used for the tests
    // The `.d.ts` declaration emit wrote for it (program_set_emit_declarations(), plan 64), null if none. Owned
    cstr* declaration_output;
    // Where its `.d.ts` goes (absolute, tsc's getDeclarationEmitOutputFilePath), set by program_typecheck() for every
    // file when declarations are emitted (a declaration file's is itself): the CLI writes there, `/// <reference path>`s
    // are rewritten relative to it. Empty otherwise
    cstr declaration_output_path;
    // declarationMap (plan 64 stage 5): the `.d.ts.map` written next to it (declaration_output_path + `.map`, null if
    // none: owned), the source's path as the map names it, the trailer's URL of the map (both set with
    // declaration_output_path)
    cstr* declaration_map;
    cstr declaration_map_source;
    cstr declaration_map_url;
    // A default lib's name in tsgo's CLI (bundled:///libs/lib.es5.d.ts, lib.d.ts for default.d.ts): tsgo reads its libs
    // from the executable, so its messages name them by that URL, not relative to the working directory
    cstr bundled_path;
    const char* display_path_override; // Used for the tests
    // Owned, in source order. Loaded in tsc's order of file.imports: the import, export and import-equals declarations
    // (and those of ambient modules) first, then import calls, import types and `require()` calls
    // (module_ref_collected_late(), tsc's collectDynamicImportOrRequireOrJsDocImportCalls)
    ModuleRef** module_refs;
    u32 module_ref_count;
    // While loading: what the load pool worker that parsed the file resolved its imports to, for the loader to take
    // rather than resolve again (program.c's, owned by the program until it has loaded the file)
    struct Preresolved* preresolved;
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
    // Where the JSDoc comments with a `@deprecated` tag end, sorted (only recorded with capture_suggestions, the
    // harness's: TS6385 is a suggestion). Owned
    const char** deprecated_jsdoc_ends;
    u32 deprecated_jsdoc_count;
    u32 deprecated_jsdoc_cap;
    // Every comment's range, sorted: start and end offsets in pairs (only recorded where declarations are emitted:
    // declaration emit copies JSDoc comments, ty/decl_comments.h). Owned
    u32* comments;
    u32 comment_count; // (pairs)
    u32 comment_cap;
    // Declarations that may be unused (noUnusedLocals/noUnusedParameters), registered by the resolve walk, see unused.h
    struct UnusedCandidate* unused; // Owned
    u32 unused_count;
    u32 unused_cap;
    bool escaped_names; // names_register_source() registered it
    // A content mapper's output (plan 56, content_mapper.h): contents is the transformed text. Owned, null for a file
    // read as written
    struct ContentMapped* content_mapped;
    // A content-mapped file's original text (ContentMapped.original), not a file of the program
    bool content_original;
} SourceFile;

// The deferred function bodies of a file (plan 78), in the resolve walk's order, and their materialization: all of them
// at once, the first time a checker needs one (program_materialize_file()); after a drop, a single body a checker of
// another file needs alone when the file's bodies are `singly`
typedef enum DeferredBodiesState : uint8_t {
    DEFERRED_BODIES_PENDING,
    DEFERRED_BODIES_READY,
    // Some of the bodies are materialized (DeferredBody.ready), the others pending
    DEFERRED_BODIES_PARTIAL,
    // Something tells the materialization from what the eager parse and walk would have made (a syntax error in a body,
    // a signature that would have seen the body's names): the program is run again without deferral
    DEFERRED_BODIES_FAILED,
} DeferredBodiesState;

typedef struct DeferredBodies {
    struct DeferredBody* first;
    struct DeferredBody* last;
    u32 count;
    u32 source_bytes; // Of the bodies
    _Atomic(uint8_t) state; // DeferredBodiesState
    // Some materialization has been through: its diagnostics are in errs (a later one's are the same, and dropped), a
    // local's assignment marks are all set (the walk only ever sets them)
    _Atomic(bool) materialized;
    pthread_mutex_t lock;
    // Plan 78 stage 2: the materialization's memory (the bodies' AST, what their walk made in the binder's arenas), held
    // by the checker incarnations that read it (Checker.held) and by a materialize-ahead helper until a checker takes
    // its ref (`ahead`), dropped by the last (deferred_bodies_release()). The bodies are then pending again, as parsed
    AstArena memory;
    u32 refs;
    bool ahead;
    // Never dropped: the first walk changed something outside the bodies a drop doesn't undo (a body's declaration
    // merged into a parameter)
    bool pinned;
    // After a drop a body may be materialized alone (DEFERRED_BODIES_PARTIAL): the first walk wrote nothing outside the
    // bodies but their functions' fields, their scopes' locals and their flow nodes' marks (deferred_bodies_undo()
    // logged no other write: no expando container, `this` assignment, field assignment)
    bool singly;
    // This materialization made some bodies alone: once all are, their unused registrations are put in walk order
    bool out_of_order;
    u32 unused_before; // SourceFile.unused_count before the bodies' walk registered theirs
    // What a drop restores: each body's function scope's locals as the signature's walk left them (in list order, made
    // by the first materialization) and the walk's writes to outer nodes (deferred_bodies_undo()). malloc'd
    struct DeferredScopeSnapshot* snapshots;
    struct DeferredUndo* undo;
    u32 undo_count;
    u32 undo_cap;
    // Materialized by a thread without a checker (program.c's materialize-ahead): the walk's shard whose pending reports
    // (they ask for types) the first checker to need the file makes. Owned
    struct ResolveShard* _Atomic unfinished;
    // What parsing and walking the bodies reported (parse errors, grammar errors, resolve errors), merged into the
    // program's at the end of program_typecheck()
    struct ErrorCtx errs;
} DeferredBodies;

// program.c: materializes the file's deferred bodies for the current checker, unless some materialization has been
// through (its walk set what symbol_assignments() reads for good)
void deferred_bodies_materialize(SourceFile const* file);
// Logs a write of `size` bytes at `at` the current materialization's walk is about to make outside its bodies (plan 78
// stage 2): a drop puts the old bytes back. Nothing outside a materialization
void deferred_bodies_undo(void* at, u32 size);
// The same for a pointer to something it owns the walk set (it was `old`): the undo drops what it set
void deferred_bodies_undo_made(void** at, void* old, void (*drop)(void*));
// A checker incarnation is dropped: its materializations are released, the last holder of one drops it
void deferred_bodies_release(struct DeferredBodies** held, u32 count);
// Readers of a local's assignment marks (Symbol.is_assigned, is_definitely_assigned, is_assigned_in_closure,
// last_assignment_end) go through this: the walks of its file's deferred bodies set them (plan 78), and code walked
// up front (an arrow function's expression body, the top level) reads them from other files' checks
static inline Symbol const* symbol_assignments(Symbol const* me) {
    if (me->is_local && me->decl_count) {
        SourceFile const* file = span_source(me->decls[0].full);
        if (file && file->deferred_bodies &&
            !atomic_load_explicit(&file->deferred_bodies->materialized, memory_order_acquire)
        ) {
            deferred_bodies_materialize(file);
        }
    }
    return me;
}

// TTTT_DEFER_STATS (debug_env.defer_stats): counts printed at exit
typedef enum DeferStat {
    DEFER_STAT_SKIPPED, // Bodies the parser deferred
    DEFER_STAT_UNDEFERRED, // ...and parsed after all (IIFEs, constructors)
    DEFER_STAT_SKIPPED_BYTES,
    DEFER_STAT_FILES, // Files materialized
    DEFER_STAT_BODIES, // Their bodies
    DEFER_STAT_CROSS, // Materializations a checker of another file asked for
    DEFER_STAT_PARSE_FAILED, // Bodies whose parse could be told from the eager one
    DEFER_STAT_COLLIDED, // Bodies whose signature could have seen their names
    DEFER_STAT_RESTARTS, // Programs run again without deferral
    DEFER_STAT_REMATERIALIZED, // Files materialized again after a drop
    DEFER_STAT_SINGLY, // ...a single body of them (DEFERRED_BODIES_PARTIAL)
    DEFER_STAT_SINGLY_COMPLETED, // Files whose bodies were materialized singly, then all
    DEFER_STAT_DROPPED, // Materializations dropped
    DEFER_STAT_PINNED, // Files whose materialization is never dropped
    DEFER_STAT_AHEAD_WAITS, // Times a materialize-ahead helper waited for the checkers
    DEFER_STAT_MATERIALIZE_US, // Time spent materializing (first materializations), microseconds
    DEFER_STAT_REMATERIALIZE_US, // ...materializing again
    DEFER_STAT_DROP_US, // ...dropping
    DEFER_STAT_COUNT,
} DeferStat;
void defer_stat_add(DeferStat stat, i64 n);

SourceFile source_file_make(cstr canonical_path, cstr contents);
// The module format the file is emitted in (tsc's getEmitModuleFormatOfFile): with node16/nodenext its implied format,
// else a .cts/.mts extension or an explicit package.json `type` decide, else the `module` option
ModuleKind source_file_emit_module_kind(SourceFile const* me);
void source_file_verbatim_cjs_error(SourceFile const* me, struct ErrorCtx* errs, Span at, bool grammar);
// tsc's getImpliedNodeFormatForEmit: MODULE_KIND_COMMONJS, MODULE_KIND_ESNEXT or MODULE_KIND_NONE (undefined: the module
// option decides)
ModuleKind source_file_implied_format_for_emit(SourceFile const* me);
void source_file_drop(SourceFile* me);
// Source text as files mapped into memory (MAP_PRIVATE): the page cache holds them anyway, and their pages can leave
// the process whenever nothing reads them (source_texts_release()) where read() copies would stay. Like clang's
// MemoryBuffer, a file changed by another process during the run is read changed (or SIGBUS if truncated). Files
// under 16 KB are read() into packed chunks instead (source_text_read()): copies, as the file was when read
// source_text_map(): the file at path mapped and wrapped in a cstr that must never be dropped or changed: the mapping's id
// (SourceFile.text_mapping), 0 (out untouched) if it can't be mapped or the mapping has no terminator after it
u32 source_text_map(csview path, cstr* out);
void source_text_unmap(u32 id);
// Drops every mapped text's pages from the process: they stay in the page cache, reading faults them back. When a
// checker is recycled (what it read is mostly what it won't read again)
void source_texts_release(void);
// Frees the mapping bookkeeping at the end of a debug run (the mappings stay: the process is exiting), so valgrind's
// leak check doesn't see it as possibly lost
void source_texts_drop(void);
// Before the process writes over the file at path: a mapping of it gets its own copy at the same address
void source_text_detach(csview path);
bool source_file_parse_error_within(Span node);
// Takes path
ModuleRef* source_file_add_module_ref(SourceFile* me, cstr path, Span span);
// Appends a ModuleRef made elsewhere (a chunked parse's, parse_file_chunked()), which the file owns from now on
void source_file_push_module_ref(SourceFile* me, ModuleRef* ref);
// tsc's getJSXRuntimeImport(getJSXImplicitImportBase()): `react/jsx-runtime` (react-jsx), `preact/jsx-dev-runtime`
// (react-jsxdev with `@jsxImportSource preact`), null if the file's JSX doesn't import its factory
cstr source_file_jsx_runtime_import(SourceFile const* me);

// Records a directive comment the parser came across (again, possibly: backtracking parses hop comments repeatedly)
void source_file_add_comment_directive(SourceFile* me, const char* start, const char* end, bool expect_error);
// The directive that the comment ending on `line` is, if any. Two on one line: the last one, like tsc's map by line
CommentDirective* source_file_comment_directive_at_line(SourceFile* me, u32 line);
// A JSDoc comment with a `@deprecated` tag the parser came across (again, possibly), and whether one ends at `end`
void source_file_add_deprecated_jsdoc(SourceFile* me, const char* end);
bool source_file_deprecated_jsdoc_ends_at(SourceFile const* me, const char* end);
// A comment the parser came across (again, possibly), recorded where declarations are emitted (SourceFile.comments)
void source_file_add_comment(SourceFile* me, const char* start, const char* end);
// The start of the comment that ends at `end`, null if none does
char const* source_file_comment_ending_at(SourceFile const* me, const char* end);

// Whether the checker walks the file: a checked one, or a plain JavaScript one for the errors that are reported there
static inline bool source_file_is_checked(SourceFile const* me) {
    return !my unchecked || my plain_js;
}

// How messages name the file: the way the test wrote it under the harness, else the canonical path
static inline csview source_file_display_path(SourceFile const* me) {
    return my display_path_override ? csview_from(my display_path_override) : cstr_sv(&my canonical_path);
}

// tsc's CLI names files in messages by their absolute path: source_file_message_path() makes paths absolute from this
// directory (the working directory, set by the CLI once before anything is loaded). Unset under the test harness,
// which names files the way its baselines do itself (display_path_override)
void source_file_set_message_cwd(csview cwd);
bool messages_use_absolute_paths(void);
// The absolute path for a path relative to the working directory, in the CLI (see source_file_set_message_cwd()), the
// path itself otherwise
cstr path_for_message(csview path);
// How message text names the file (a module "from external module ...", TS2306's file): its display path, absolute in
// the CLI
static inline cstr source_file_message_path(SourceFile const* me) {
    if (my is_stdlib && cstr_size(&my bundled_path) && messages_use_absolute_paths()) return cstr_clone(my bundled_path);
    return path_for_message(source_file_display_path(me));
}

// How messages name the file a module name found (TS2306)
static inline cstr module_ref_file_message_path(ModuleRef const* me) {
    return source_file_message_path(module_ref_rare(me)->redirected_from ? module_ref_rare(me)->redirected_from : my file);
}

// What there is to say about a module name that did find its file (TS5097)
// type_only: of an `import type`/`export type` declaration or a side effect import (no TS2846 for naming a .d.ts)
void module_ref_report_resolved(ModuleRef const* me, bool type_only, bool side_effect, struct ErrorCtx* errs);
// tsgo's isInternalDeclaration: a leading comment of the declaration starting at `at` says `@internal` (block comments
// and whole-line `//` comments right before it)
bool source_text_internal_before(struct SourceFile const* me, char const* at);
void module_ref_report_resolved_import_type(ModuleRef const* me, struct ErrorCtx* errs);
// TS1471/TS1479: a CommonJS use of an ES module under node16/node18, reported at `at`
void module_ref_report_require_of_esm(ModuleRef const* me, Span at, struct ErrorCtx* errs);

// The diagnostic for a module name that led to no module (neither a file nor an ambient module): TS2307 and its
// flavours, by what module resolution found instead
void module_ref_report_unresolved(ModuleRef const* me, struct ErrorCtx* errs);
// The same for a side effect import (`import "x"`): TS2882 instead of TS2307
void module_ref_report_unresolved_side_effect(ModuleRef const* me, struct ErrorCtx* errs);
// TS7016 at `at` for a module that resolved to JavaScript (MODULE_REF_IS_JS), under noImplicitAny (tsc's
// errorOnImplicitAnyModule)
void module_ref_report_implicit_any_at(ModuleRef const* me, Span at, struct ErrorCtx* errs);

// 0-based index of the line `pos` (which must point into the contents, or at their end) is on
unsigned source_file_line_index(SourceFile const* me, const char* pos);

// tsc's getErrorSpanForNode of a source file: its first token after trivia
Span source_file_first_token(SourceFile const* file);

static inline const char* source_file_start(SourceFile const* me) {
    return cstr_str(&my contents);
}
