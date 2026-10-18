#pragma once

#include "prelude.h"
#include "compiler_options.h"

#include <cleat/filesystem.h>

// What a module name or a type reference directive refers to: tsc's moduleNameResolver.ts. A pure function of the
// filesystem, the options, the file that contains the name, the name and the mode it is resolved in. Knows nothing of
// programs, syntax trees or symbols. All paths in here are absolute and normalised

typedef struct ModuleResolver ModuleResolver;

// Which conditions a package.json `exports` is read with under node16/nodenext: `import` or `require`
typedef enum ResolutionMode {
    RESOLUTION_MODE_COMMONJS,
    RESOLUTION_MODE_ESM,
    // CommonJS because the syntax says so (`import x = require()`, `resolution-mode: "require"`): bundler resolution,
    // which otherwise always reads `exports` with `import`, uses `require` then too
    RESOLUTION_MODE_REQUIRE_SYNTAX,
} ResolutionMode;

typedef enum ResolvedKind {
    RESOLVED_NONE, // Nothing found
    RESOLVED_TS, // .ts, .tsx, .mts, .cts
    RESOLVED_DTS, // .d.ts, .d.mts, .d.cts, .d.*.ts
    RESOLVED_JS, // .js, .jsx, .mjs, .cjs
    RESOLVED_JSON,
} ResolvedKind;

typedef struct ResolvedModule {
    ResolvedKind kind;
    cstr path; // Absolute, empty if nothing was found
    bool is_external_library; // Found in a node_modules
    bool arbitrary_extension; // `./a.css` found as a.d.css.ts: TS6263 without allowArbitraryExtensions
    bool used_ts_extension; // The specifier itself ended in .ts/.tsx/.mts/.cts (not .d.ts): TS5097 without the option
    bool ts_extension_from_star; // A package.json imports/exports `*` matched the specifier's TS extension
    // Failure hints, for the caller's choice of message
    bool needs_extension; // A relative name without extension in ESM mode of node16/nodenext: TS2834/TS2835
    cstr suggested_extension; // With needs_extension: ".js", ".mjs"... when the file that was meant exists
    bool json_without_option; // Would have been found with resolveJsonModule: TS2732
    // tsc's packageId: `name/sub/module.d.ts@version` for a file of a node_modules package whose package.json has a name
    // and a version, else empty. The program takes a second file with the same id for the first (duplicate packages)
    cstr package_id;
    cstr package_name; // tsc's packageId.name: the package.json's name, with package_id
    // Where path was before symlinks were followed (tsc's originalPath), empty if that changed nothing: what the
    // program's symlink cache learns links from (module_specifiers.h)
    cstr original_path;
    // tsc's alternateResult (absolute path): the declaration file a resolution without `exports` (node16/nodenext/
    // bundler) or with moduleResolution bundler (node10) would find, when this one found none
    cstr alternate_result;
    // tsc's TS2209/TS2210 (found or not): an exports/imports entry of a package.json (absolute path) pointed into the
    // outDir without a rootDir that says which input directory the outputs mirror
    cstr ambiguous_root_entry; // "." for the main export
    cstr ambiguous_root_package_json; // Empty when there's nothing to report
    bool ambiguous_root_in_imports;
} ResolvedModule;

void resolved_module_drop(ResolvedModule* me);
ResolvedModule resolved_module_clone(ResolvedModule const* me);

// fs and options must outlive the resolver. Options are read at resolution time, so they may still change after this
ModuleResolver* module_resolver_new(Fs const* fs, CompilerOptions const* options);
void module_resolver_destroy(ModuleResolver* me);
// Drops the cache of resolutions (once the program's files are loaded nothing asks again: 33 MB on vscode). Not while
// another thread resolves
void module_resolver_forget_resolutions(ModuleResolver* me);

// Where a path leads once symlinks are followed (absolute in, absolute out). Without one paths are taken as they are.
// Like tsc, only what is found in a node_modules is looked up (and not with `preserveSymlinks`), so that a linked
// package is one set of files however it was reached
typedef cstr (*RealpathFn)(void* ctx, csview path);
void module_resolver_set_realpath(ModuleResolver* me, RealpathFn realpath, void* ctx);

// The outputs of referenced projects exist for this resolver, built or not (project.h): their declaration files and
// directories. The caller turns an output it finds into its source. redirects must outlive the resolver
typedef struct ProjectRedirects ProjectRedirects;
void module_resolver_set_redirects(ModuleResolver* me, ProjectRedirects const* redirects);
// Where path leads once symlinks are followed, for a path that may not exist (a referenced project's output that was
// never built, through a workspace link: its nearest existing directory is followed), with redirects. Empty if the
// resolver has no realpath or nothing could be followed
cstr module_resolver_real_path_lenient(ModuleResolver* me, csview path);
// The package id a resolution through the package's own `exports` would give a file outside node_modules (tsc's
// getPackageId of the nearest package.json with a name), empty if none
cstr module_resolver_source_package_id(ModuleResolver* me, csview path);

// What the reverse direction (module_specifiers.c) asks of the resolver's filesystem view and caches
// The parsed package.json in directory (absolute), null if there is none. Cached; lives as long as the resolver
struct JsonValue const* module_resolver_package_json(ModuleResolver* me, csview directory);
// A file exists, referenced projects' outputs included (see module_resolver_set_redirects())
bool module_resolver_file_exists(ModuleResolver* me, csview path);
// An option's path made absolute: relative to the tsconfig it came from, else to the current directory
cstr module_resolver_option_path(ModuleResolver* me, csview path);
csview module_resolver_cwd(ModuleResolver* me);
// The `paths`-like object of a package.json's first `typesVersions` entry that applies to tsc's version, null if none
struct JsonValue const* module_resolver_types_versions(struct JsonValue const* package_json);
// Whether a semver range ("types@>=5.0"'s ">=5.0") covers tsc's version
bool module_resolver_version_in_range(csview range);

// Both take any path for containing_file (relative ones are relative to the fs' working directory)
ResolvedModule module_resolver_resolve(
    ModuleResolver* me,
    csview specifier,
    csview containing_file,
    ResolutionMode mode
);
// tsc's resolvePackageDirectory: the directory of package `name` in the nearest node_modules (or its @types twin) up
// from containing_file, whether or not anything in it resolves; path is followed through symlinks (original_path is
// the link). Empty path if there is none
ResolvedModule module_resolver_resolve_package_directory(ModuleResolver* me, csview name, csview containing_file);
// `/// <reference types="name" />` and the entries of `types`. An empty containing_file is the options themselves
// A `/// <reference types>` or `types` entry. mode is the reference's `resolution-mode` or else the containing file's
// format, null if it has none (tsc's getModeForFileReference)
ResolvedModule module_resolver_resolve_type_reference(
    ModuleResolver* me, csview name, csview containing_file, ResolutionMode const* mode
);

// The type reference directives that are part of every program: the `types` option, else every package in the type
// roots (`typeRoots`, else each `node_modules/@types` from the config's or the current directory upwards)
StrList module_resolver_automatic_types(ModuleResolver* me);

// What node16/nodenext make of a file: an ES module by extension (.mts/.mjs, not .cts/.cjs), else by the `type` of the
// nearest package.json (tsc's impliedNodeFormat and packageJsonScope)
typedef struct FileFormat {
    ResolutionMode mode;
    cstr package_json; // The nearest package.json, empty if there is none
    bool package_has_type;
} FileFormat;
FileFormat module_resolver_file_format(ModuleResolver* me, csview file);

// A DOS drive root, `c:/x` or `c:`: tsc's isRootedDiskPath knows it, POSIX paths don't. tttt (and its mock file
// system) treat it as a directory named `c:` in the current directory, what a POSIX system does with it
static inline bool path_is_drive_rooted(csview path) {
    if (path.size < 2 || path.buf[1] != ':') return false;
    char c0 = path.buf[0];
    if (!((c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z'))) return false;
    return path.size == 2 || path.buf[2] == '/' || path.buf[2] == '\\';
}

static inline bool module_name_is_relative(csview name) {
    // tsc's isExternalModuleNameRelative(): "./", "../", ".", ".." (either slash), or rooted (isRootedDiskPath: POSIX or
    // UNC, a DOS drive `c:/` or `c:`). Not a URL `scheme://authority/`: getEncodedRootLength encodes its root as a
    // negative length, which isRootedDiskPath's `> 0` excludes
    if (!name.size) return false;
    char c0 = name.buf[0];
    if (c0 == '/' || c0 == '\\') return true;
    if (c0 == '.') {
        if (name.size == 1 || name.buf[1] == '/' || name.buf[1] == '\\') return true;
        return name.buf[1] == '.' && (name.size == 2 || name.buf[2] == '/' || name.buf[2] == '\\');
    }
    bool volume = (c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z');
    return volume && name.size >= 2 && name.buf[1] == ':' && (name.size == 2 || name.buf[2] == '/' || name.buf[2] == '\\');
}
