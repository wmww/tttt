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
    // Failure hints, for the caller's choice of message
    bool needs_extension; // A relative name without extension in ESM mode of node16/nodenext: TS2834/TS2835
    cstr suggested_extension; // With needs_extension: ".js", ".mjs"... when the file that was meant exists
    bool json_without_option; // Would have been found with resolveJsonModule: TS2732
} ResolvedModule;

void resolved_module_drop(ResolvedModule* me);

// fs and options must outlive the resolver. Options are read at resolution time, so they may still change after this
ModuleResolver* module_resolver_new(Fs const* fs, CompilerOptions const* options);
void module_resolver_destroy(ModuleResolver* me);

// Where a path leads once symlinks are followed (absolute in, absolute out). Without one paths are taken as they are.
// Like tsc, only what is found in a node_modules is looked up (and not with `preserveSymlinks`), so that a linked
// package is one set of files however it was reached
typedef cstr (*RealpathFn)(void* ctx, csview path);
void module_resolver_set_realpath(ModuleResolver* me, RealpathFn realpath, void* ctx);

// Both take any path for containing_file (relative ones are relative to the fs' working directory)
ResolvedModule module_resolver_resolve(
    ModuleResolver* me,
    csview specifier,
    csview containing_file,
    ResolutionMode mode
);
// `/// <reference types="name" />` and the entries of `types`. An empty containing_file is the options themselves
ResolvedModule module_resolver_resolve_type_reference(ModuleResolver* me, csview name, csview containing_file);

// The type reference directives that are part of every program: the `types` option, else every package in the type
// roots (`typeRoots`, else each `node_modules/@types` from the config's or the current directory upwards)
StrList module_resolver_automatic_types(ModuleResolver* me);

// Whether a file is an ES module to node16/nodenext: by extension (.mts/.mjs/.cts/.cjs), else by the `type` of the
// nearest package.json
ResolutionMode module_resolver_file_mode(ModuleResolver* me, csview file);

static inline bool module_name_is_relative(csview name) {
    // tsc's isExternalModuleNameRelative(): "./", "../", ".", "..", or rooted
    if (!name.size) return false;
    if (name.buf[0] == '/') return true;
    if (name.buf[0] != '.') return false;
    if (name.size == 1 || name.buf[1] == '/') return true;
    return name.buf[1] == '.' && (name.size == 2 || name.buf[2] == '/');
}
