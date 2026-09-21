# Plan 06: module resolution

## Why
`program.c` resolves a module name with ~120 lines of guesswork (`get_import_path*`): relative path + `.d.ts`/`.ts`,
`index`, the *first* `node_modules` directory found walking up, `@types` inside it. No package.json, no `paths`, no
type reference directives, no `@types` inclusion, no node16/bundler rules, no tsconfig. Spurious TS2307 is in 373 tests
(`./failstats.py --code TS2307`), by what they need (overlapping): importing `.js`/allowJs 194, package.json 125,
node_modules 121, node16/nodenext/bundler 87, expect TS2792 (classic resolution's flavour of the message) 45, json
modules 39, tsconfig.json in the test 38, baseUrl 22, symlinks (`@link`) 20, `paths` 16, `@currentDirectory` 11.
On the real corpus the CLI is handed `@types/node` and the options by hand (`realworld.py`).

## Design
Module resolution is a **pure function of (filesystem, options, containing file, specifier, mode)**, like tsc's
`moduleNameResolver.ts`, in its own file with no knowledge of `Program`, the AST or the binder. That keeps it testable
by hand, cacheable, and callable from parallel per-file loading later (its caches are the only state; they'd need a
lock or to be per-thread).

- `src/json.{h,c}`: a small JSONC parser (comments, trailing commas: tsconfig) with **ordered** object members (the
  condition order of package.json `exports` is significant; cleat's `SeriValue` map is an unordered hmap).
- `src/module_resolution.{h,c}`: `ModuleResolver` (fs, options, cwd, package.json cache by directory).
  - `module_resolver_resolve(me, specifier, containing_file, mode, out)`: `paths` / `baseUrl` / `rootDirs` first
    (non-relative names resp. relative ones), then by `moduleResolution`:
    - classic: relative = file with extensions; non-relative = walk up from the containing directory trying
      `dir/name.*`, then `node_modules/@types` walking up.
    - node10: file (extension probing, `.js` -> `.ts`/`.tsx`/`.d.ts` substitution), directory (package.json
      `typings`/`types`/`main`, `typesVersions`, `index`), `node_modules` walking up (`name`, then `@types/name` with
      `@scope/x` -> `scope__x`).
    - node16/nodenext/bundler: the above plus package.json `exports`/`imports` with conditions (`types`, `import`
      or `require` by mode, `node` unless bundler, `customConditions`, `default`), subpath patterns, self-name
      imports, `.mts`/`.cts`/`.d.mts`/`.d.cts`, no extension/index probing for ESM-mode relative imports in node16.
    - `moduleSuffixes`, `resolveJsonModule`, `allowJs` (a `.js` result is a resolution, flagged `is_js`).
  - `module_resolver_resolve_type_reference(me, name, containing_file, out)`: primary lookup in `typeRoots` (default:
    every `node_modules/@types` walking up from the config/current directory), secondary = node_modules lookup from
    the containing file.
  - `module_resolver_automatic_types(me)`: the `types` option, else every package in the type roots.
  - The result says *what kind of failure* it was so the caller can pick tsc's message.
- `CompilerOptions` gains `base_url`, `paths`, `root_dirs`, `type_roots`, `types`, `module_suffixes`,
  `custom_conditions`, `config_dir` (what relative paths in the above are relative to). Strings/lists as written in a
  tsconfig; `compiler_options_set()` takes lists comma separated (the harness directives' syntax).
- `src/tsconfig.{h,c}`: `tsconfig_load(fs, path, options, root_files)`: `extends` (relative and node_modules),
  `compilerOptions`, `files` / `include` / `exclude` (globs: `*`, `?`, `**/`), `${configDir}`. Used by the CLI
  (`tttt -p dir|file`, and `tttt` with no files looks for `tsconfig.json` upwards) and by the harness (a test unit
  named `tsconfig.json` gives the options and the root files, like `compilerRunner.ts`).
- Program: `program_load()` resolves through the resolver; `ModuleRef` keeps the failure kind; `/// <reference
  types>` goes through type reference resolution; automatic `@types` files are loaded as roots
  (`program_load_automatic_types()`). Paths of files become absolute + normalised internally? **No**: keep
  `canonical_path` relative to cwd as today (error messages and baselines print it), resolve with absolute paths and
  convert at the boundary.
- Harness fidelity: tsc's harness puts relative `@Filename`s in `/.src` and that is the cwd, so `/node_modules` is
  found walking up but is not below the sources. `@currentDirectory`, `@link`/`@symlink` (mock fs symlinks + realpath;
  resolved modules are realpath'ed unless `preserveSymlinks`), `@baseUrl`/`@typeRoots`/`@types`/... directives.
- Diagnostics: TS2792 instead of TS2307 when resolution is classic, TS7016 (resolved to JS without allowJs, under
  noImplicitAny; silently `any` otherwise), TS2688 (type reference / `types` entry not found), TS2732 (json without
  `resolveJsonModule`), TS2834/TS2835 (extensionless relative import in node16 ESM), TS5097/TS2691 (`.ts` extension
  in a specifier), TS6053 (`/// <reference path>` not found).

## Stages (each ends with `./fasttest.sh --summary`, no component may regress on balance, commit)
1. Options + json + resolver core (classic, node10, paths/baseUrl/rootDirs, package.json types/main/typesVersions,
   moduleSuffixes), program wired to it, the old `get_import_path*` deleted, harness cwd `/.src` +
   `@currentDirectory`, TS2792.
2. Type reference directives, `typeRoots`/`types`, automatic `@types`, TS2688. `.js` results + TS7016, json modules.
3. node16/nodenext/bundler: exports/imports/conditions, `.mts`/`.cts`, file mode (package.json `type`), TS2834/2835.
4. tsconfig: CLI (`-p`, discovery) and harness; `realworld.py` runs `tttt -p thirdparty/tsc/src/compiler` with no
   hand-passed options or `@types/node` and must report the same or fewer diagnostics; symlinks in the mock fs.

## Validation
- `./failstats.py --code TS2307`: 373 tests now; target < 100, the rest explained in `triage.md` by what they need.
- The ~50 tests the root file rule made honest (typeReferenceDirectives*, pathMappingBasedModuleResolution*,
  nodeModulesTripleSlashReference*) pass their errors component again.
- `./realworld.py` works from the corpus' tsconfig alone; its last name error (`source-map` via
  `@types/source-map-support`'s package.json) is gone.
- `./bench.sh`: load + parse not measurably slower (the resolver caches package.json lookups per directory).

## Not in scope
Real JS/JSDoc support (a resolved `.js` file is still parsed as TypeScript: the JS gate is the triage session's),
project references, `--build`, watch, declaration emit paths, TS1479 (CJS importing ESM) and the other
module-*format* checks beyond what falls out of knowing a file's mode.
