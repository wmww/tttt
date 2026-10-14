#pragma once

#include "prelude.h"
#include "module_resolution.h"

// The reverse of module resolution: the module specifier a declaration file would write to name a file from another
// one (tsc's moduleSpecifiers.ts getModuleSpecifiers, as the declaration emitter asks it: `project-relative`
// preference, not for auto-imports). tttt doesn't emit; declaration diagnostics need the specifier to know whether it
// reaches into a node_modules (TS2742). Like module resolution, a function of the filesystem, the options and what the
// program learned about symlinks. All paths absolute and normalised

// Where the program met symlinked directories (tsc's SymlinkCache, getSymlinkedDirectoriesByRealpath): learned from
// every resolution that followed a link, and from resolving the dependencies of the importing file's package.json
// (tsc's getAllModulePathsWorker). Thread safe
typedef struct SymlinkCache SymlinkCache;
SymlinkCache* symlink_cache_new(void);
void symlink_cache_destroy(SymlinkCache* me);
// A resolution found `original` to be `real` (ResolvedModule.original_path and path)
void symlink_cache_add_resolution(SymlinkCache* me, csview original, csview real);

// tsc's ResolutionMode: undefined, CommonJS, ESNext
typedef enum SpecifierMode : uint8_t {
    SPECIFIER_MODE_NONE,
    SPECIFIER_MODE_COMMONJS,
    SPECIFIER_MODE_ESM,
} SpecifierMode;

typedef struct SpecifierImport {
    csview text;
    SpecifierMode mode; // getModeForUsageLocation
} SpecifierImport;

typedef struct SpecifierRequest {
    ModuleResolver* resolver; // Of the importing file's options
    CompilerOptions const* options;
    SymlinkCache* symlinks;
    csview importing;
    SpecifierMode importing_format; // impliedNodeFormat
    SpecifierMode importing_default_mode; // getDefaultResolutionModeForFile
    // The importing file's import specifiers (sourceFile.imports), for the extension it prefers
    SpecifierImport const* imports;
    u32 import_count;
    SpecifierMode override_mode;
    SpecifierMode ending_mode; // Else override_mode, else importing_default_mode: ESM prefers the `.js` ending
    csview target; // The file to name (a referenced project's source by its source)
    csview redirect; // Its output declaration file if it's a referenced project's source, tried first, else empty
    csview project_dir; // The tsconfig's directory, else empty (the current directory)
} SpecifierRequest;

// The best specifier (getModuleSpecifiers' first): a package name through a node_modules when one can reach the file, a
// `paths` mapping, else relative (which may go through some node_modules, which is what TS2742 is about). Existing
// imports of the file are the caller's to look at first (computeModuleSpecifiers' existingSpecifier)
cstr module_specifier_generate(SpecifierRequest const* request);
