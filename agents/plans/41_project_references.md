# Plan 41: project references
Planned 2026-09-29 (todo entry 9 of the seventh window). Three stages, stage 1 is one to two sessions.

## Status (2026-10-04)
**All three stages done** (todo entry 6 of the eighth window): tsc-src and typescript-eslint at 0 under `-b`;
`scripts/scenarios.py` 213 pass / 5 fail (TS6377 out of scope, TS2613/TS1192, TS1479: triage.md; the CLI's phase gating,
config syntax errors and a missing `extends` were fixed 2026-10-05: `diagnostic_kind()` in main.c,
`program_set_config_file()`, `tsconfig_load_diagnosed()`; leftovers in hacks.md "Config syntax"). What remains is perf (parallel name resolution of referenced sources, the
biggest phase of `-b` on both corpora). The stage notes below are as they were written.

**Stage 1 done** in one session (todo entry 3 of the eighth window): `src/project.{h,c}`, the redirects, `-p`/`-b`,
`scenarios.py`, corpus `tsc-src`. documentation.md "Project references" describes what was built. Numbers:
- `scripts/realworld.py --corpus tsc-src` (`-b thirdparty/tsc/src`): 12 diagnostics, the 11 checker bugs below plus
  services.ts:2434 (TS2345 `boolean | UserPreferences`), which only shows with 4 checkers: `preferences ?? emptyOptions`
  subtype-reduces `{}` against the weak `UserPreferences` by type id order (tsc's strictSubtype tie break; triage).
  3.65 s wall, 755 MB peak (tsc -b 15.6 s); threads 1 and 4 byte-identical.
- `scripts/scenarios.py`: 199 pass, 19 fail, 82 skipped with reasons. The 19: TS1005 in tsconfig (tttt's JSON reader
  doesn't report syntax errors), TS4023/TS4094 (plan 40: a declaration walk gap, and none at all with `--noCheck`),
  TS6377 (tsbuildinfo, out of scope), extends of a missing file (tsc: TS5083 and goes on; tttt fails the config), the
  CLI's phase gating, TS2613/TS1192 and an ESM-to-ESM synthetic default under bundler, TS1479 on a JSX runtime import,
  `--composite null`. All in triage.md.
- Stage 2's profile (`--time`, summed over the 13 programs): resolve names 1.48 s, load+parse 0.78 s, typecheck
  0.55 s, libs 0.05 s. **Name resolution of referenced projects' files, redone in every program, is the cost**; checking
  only the project's own files already made typecheck small. Teardown between programs isn't visible in the total.

**Stage 3 part 1 done** (2026-10-04): tsc-src is at **0** (at 1 and 4 checkers). The 12 were 5 mechanisms: an
intersection property's optional member lacking its undefined (callHierarchy), a short tuple padded with undefined
in a union no member of which has the element as a property (fixNaNEquality, fixAddMissingConstraint), fresh vs
regular literals in inference's matching pass (findAllReferences), a merged interface taking its type parameters
from a parameterless first declaration (mocha's `NodeJS.EventEmitter` forward declaration: all of testRunner), and
widened object literals still counting as object literals for requireOptionalProperties (services.ts:2434: not a
type-id tie after all, `{}` simply isn't a strict subtype of the weak type). Left: the second corpus.

**Stage 2 done** (todo entry 4 of the eighth window, one session): `-b thirdparty/tsc/src` 3.65 s / 755 MB -> **1.32 s /
785 MB** (best of 3; `--build-jobs 1`: 1.74 s / 540 MB). tsgo -b on a /tmp copy 2.34 s / 577-876 MB (it emits: noEmit
isn't allowed in referenced projects), single-threaded 3.45 s; tsc -b 13.6 s. Output identical at every
`--build-jobs`/`--threads`. What did it, in order of effect:
1. Reference sources' function bodies with a written return type are neither parsed nor resolved (a brace scanner,
   `skip_typed_body()`; TypeScript's compiler/ is isolatedDeclarations, so nearly every body). Resolve names summed
   1.48 -> 0.58 s, load+parse 0.72 -> 0.32 s, free 0.55 -> 0.22 s.
2. Two projects at once (`--build-jobs`, main.c's `Output` buffers printed in build order).
3. `program_drop()` frees the files on the program's threads (teardown was ~65 ms per program, ~0.85 s in all).
Tried and reverted: freeing finished programs on the idle main thread (malloc arena contention, 2 GB peak). Not done:
a cross-program file cache (parsing is no longer the cost), parallel name resolution (now the biggest phase of `-b`,
0.58 s summed, serial: vision's later list; it helps every corpus too). `--time` split now (J=1, summed): resolve 583,
typecheck 441, load+parse 316, free 217, libs 74 ms.

**Stage 3, typescript-eslint done** (2026-10-04, one session; the 11 tsc-src checker bugs are another agent's): corpus
**`typescript-eslint`** pinned (v8.57.2, 1bf86c9f, the last commit before TypeScript 6; `scripts/realworld.py --corpus
typescript-eslint` runs `tttt -b` at its root solution: 38 composite projects, nested per-package solutions,
`${configDir}`, NodeNext, workspace packages through node_modules symlinks into `dist/*.d.ts`). ensure-corpora.sh's LINK
field takes `workspace:GLOB`; the overlay has the two generated files (types' ast-spec.ts, the website's
recent-blog-posts.json). tsc 5.8 -b on a /tmp copy: 0 errors, 21-23 s, 1.5-2.3 GB. **66 -> 0 diagnostics**, none of
them the references mechanism (resolution through the workspace symlinks and the redirect worked first time): 30
grammar errors of skipLibCheck'd .d.ts files, 137 declaration diagnostics (TS4082/TS4023 through an `export =`
namespace merge, visible once eslint-plugin had no type errors) and 9 checker bugs (partial mapped types, async
`Promise<void>` return context, Omit inference, top signatures, distributive constraints of `T & {}`, readonly through
a narrowed `this`, `switch (typeof x?.y)`, fresh `{}` binding defaults, overload failure's longest candidate). tttt
-b 3.0 s / 650-700 MB (`--build-jobs 1` 3.4-3.8 s / 400 MB) on a loaded machine; summed phases at J=1: resolve names
1.03 s, load+parse 0.74, typecheck 0.69, free 0.47, libs 0.21 (38 programs: per-program name resolution of reference
sources is again the cost, stage 2's unfinished item). Not in bench.sh/instrcount.sh (tsc -b alone is 21 s).

## Why
tttt ignores `references`. Two kinds of real projects use them:
- **Monorepos built with `tsc -b`**: a solution tsconfig (`"files": []` + `references`), one composite project per
  package, packages importing each other through workspace symlinks whose package.json points at `dist/*.d.ts`, or
  through relative paths straight into the other package's sources (TypeScript's own `src/`).
- **Every Vite template** (`npm create vite`): `tsconfig.json` references `tsconfig.app.json` and `tsconfig.node.json`
  (xstate's `templates/` and `examples/` have 13 of them); the script is `tsc -b && vite build`.

Today `tttt -p` on a solution config says "No input files", and on a referencing package it checks the referenced
package's files as its own, under the wrong options.

**Measured on TypeScript's `src/` (a /tmp copy; `tsc -b src` is clean under 5.8.2 once `scripts/failed-tests.cjs` is
there, which the checkout has: 15.6 s wall).** `tttt -p src/X` for each of the 13 projects today: compiler, jsTyping,
typingsInstallerCore, watchGuard 0; deprecatedCompat, services, tsc, typescript 66-72; server, harness, tsserver,
typingsInstaller 6; testRunner 11. Of those:
- **66 are the mechanism**: all in `compiler/` files (sys.ts, core.ts, tracing.ts, performanceCore.ts, debug.ts:
  TS2591/TS2307/TS2304 on `require`/`process`/node modules), which the referencing project reaches by relative imports
  (`services/_namespaces/ts.ts`: `export * from "../../compiler/_namespaces/ts.js"`) and checks under its own
  `types: []`. compiler's tsconfig has `types: ["node"]`. Under tsc -b those files are compiler's, checked once with
  compiler's options.
- **11 are ordinary checker bugs** in code only these projects have: services/callHierarchy.ts:199 (TS2339 on
  `never`), :313 (TS18046 twice), codefixes/fixAddMissingConstraint.ts:110 and fixNaNEquality.ts:69 (TS2322 with
  `| undefined`), findAllReferences.ts:1193 (TS2345 with `| string`), testRunner/parallel/host.ts:176/181/185 (node's
  `WriteStream & { fd: 1 }` to `WritableStream`: overload set relation), worker.ts:34/35 (`Key<string, T>`: node's
  EventEmitter generic). They reproduce with `-p src/services` / `-p src/testRunner` today; stage 3 of this plan.

The suite has no multi-project tests (`"references"` appears in one compiler test, as data); the composite tests pass
already. The oracles are tsc's own tsbuild/tsc scenario baselines (see Validation) and corpora.

## Decision: tsc's source redirect, one program per project
tsc -b emits each referenced project's `.d.ts` and the referencing project reads those; tttt has no emit. tsc's
language service (and tsgo's LSP) instead uses **useSourceOfProjectReferenceRedirect**: an import that resolves to a
referenced project's output `.d.ts` (or to its source directly) gets the *source file*, which is part of the program
but never checked or reported (it's the referenced project's to report). For a project that is clean under tsc -b this
gives the same answer (editors show exactly this), and it needs no build order for correctness. Adopt it for both
`-p` and `-b`. (2026-10-08, plan 52: `-p` now does what tsc's command line does instead, outputs and TS6305; the
source sweep showed the unbuilt-output cascade in ~25 repositories.)

Confirmed in `program.ts` (updateHostForUseSourceOfProjectReferenceRedirect, findSourceFileWorker,
getRedirectReferenceForResolution, getCompilerOptionsForFile):
- The host's fileExists is true for a referenced project's output `.d.ts` paths whose source exists, directoryExists
  for its declaration directories (`declarationDir` else `outDir`) and their parents, getDirectories only for real
  ones. So module resolution *through package.json* (`types: ./dist/index.d.ts`, `exports` conditions) finds outputs
  that were never built, and findSourceFile swaps in the source.
- The mapping covers every project reachable through `references`, transitively (forEachResolvedProjectReference).
  Output path = getOutputDeclarationFileName: relative to `rootDir` (composite: the tsconfig's directory by default;
  else the common source directory of the files), under `declarationDir ?? outDir` (next to the source without
  either), `.ts/.tsx` -> `.d.ts`, `.mts` -> `.d.mts`, `.cts` -> `.d.cts`; `.d.ts` and `.json` inputs are not mapped.
  A referenced project with `outFile` or `module: none` contributes all its files up front instead.
- **Module resolution in a referenced source file uses the referenced project's options** (paths, baseUrl,
  moduleResolution, customConditions, module format: getCompilerOptionsForFile); everything else (parse target,
  checker strictness, automatic `types`, lib) is the referencing program's. So compiler's `process` is unresolved in
  services' program, silently: nothing in compiler's files is reported there.
- Root files of the referencing project that belong to a referenced project are redirected the same way.

Divergences by design, documented in documentation.md when built:
- `tsc -p pkg` without built outputs reports TS6305 ("Output file ... has not been built from source file") per
  import; with stale outputs it reads them. tttt always reads sources: no TS6305, never stale.
- A referenced project's declarations are typed under the referencing project's checker options (strictness), where
  tsc -b reads the `.d.ts` text printed under the referenced project's options (e.g. `let x = null` is `any` in a
  non-strict package's `.d.ts`, `null` in a strict reader). Same as tsc's editor behaviour. Revisit only if a corpus
  shows it.
- tsc -b's d.ts may carry `/// <reference types="node" />`, leaking node's globals into the referencing project; with
  sources they don't leak. A project that only compiles thanks to that leak would get errors from tttt (none of
  TypeScript's do).

**Rejected: one program for the whole solution** (each file with its project's options). It would check every file
once, but projects have separate global scopes (testRunner's mocha globals, compiler's node globals, two packages'
`declare global`s must not see each other), checker options are per program, and scripts of two projects would merge
or conflict. Sharing parsed/bound files between the per-project programs is the perf question of stage 2.

## Design
- **`src/tsconfig.c`** reads `references` (`path`: a tsconfig or a directory with a tsconfig.json; `circular`;
  ignore `prepend`) into absolute paths, with the span of each entry for diagnostics.
- **A new `src/project.{h,c}`** (the only code that knows about more than one config): loads a config and, recursively,
  everything it references, each once by canonical path: `Project { config path, CompilerOptions (resolved), root
  files, references }`. tsc's build order for `-b` (getBuildOrder: depth-first post-order over `references` in
  written order, from each command-line project; a cycle is TS6202 unless `circular`). The output map of each project
  (output `.d.ts` path -> source, declaration directories), computed lazily, shared by every program that needs it.
  Config diagnostics, reported on the referencing config at the reference's span like tsc's options diagnostics
  (`program_set_config_file()`): TS6053 (referenced config not found), TS6306 (referenced project without
  `composite`, unless it's a container: `files: []` and no `include`), TS6310 (referenced project with `noEmit`),
  TS6202; and composite's own TS6304 (declaration false), TS6379 (incremental false), TS6307 (a composite project's
  program contains a non-declaration file its `files`/`include` don't list: it came in by import), TS6059 (a file
  outside `rootDir`). Check which the suite or the mined scenarios exercise before doing all of them; TS6377 and
  tsbuildinfo matters are out of scope.
- **`Program`** gets the referenced projects (`program_set_references()`, before loading). The loader asks the map
  whenever it resolves a module, a `/// <reference path>`, or takes a root: an output path becomes the source path;
  a file that *is* a referenced project's source (whichever way it was reached) is marked `SourceFile.
  from_reference` (the project index), is `unchecked`, and its parse and name-resolution diagnostics are dropped
  (check what `unchecked` covers today: it was made for skipLibCheck `.d.ts`, whose names are resolved and whose
  errors may still be reported in the resolve phase). Its module refs resolve with its project's options: the program
  keeps one `ModuleResolver` per option set (the resolver already takes its options at construction and caches per
  resolver), and the resolution-mode / file-format helpers take the file's options. The resolver's file and
  directory existence checks consult the output map (a hook on `ModuleResolver`, or a wrapping `Fs`: the
  implementer's call; realpath through workspace symlinks must still land on the canonical source path). The load
  pool's load-ahead follows the same redirects, or at least doesn't parse outputs that don't exist.
- **CLI (`main.c`)**: `-p` on a config with references checks that project with the redirects. `-p` on a solution
  config (no files, only references) reports nothing and exits 0, like tsc (print a hint to use `-b`). New `-b
  [PATHS...]` (default `.`): every project in build order, each its own `Program` with its own options, the
  diagnostics of each printed as it finishes, one summary line, exit 1 if any project had errors. tsc 5.6+ keeps
  building past a project with errors: do the same. Accept and ignore `--verbose`/`--force`/`--dry`/`--clean` or
  reject them clearly. `check_files()` becomes "check one project"; release builds skip freeing the program at exit
  today, but with `-b` each program must be freed (or its arenas reused) before the next: measure the teardown (the
  AST free was a tenth of a run) and fix it if it shows.
- **Test harness**: stays single-program; a `tsconfig.json` unit with `references` gets the redirects (tsc's
  compilerRunner doesn't pass project references, so nothing in the suite changes: check `scripts/fasttest.sh` shows 0
  lost).

## Stages
### Stage 1: semantics (1-2 sessions)
tsconfig `references`, `project.c`, the redirects in the loader and resolver, per-file resolution options, unchecked
reference sources, `-p` and `-b`, the config diagnostics the scenarios need. Pin TypeScript's `src/tsconfig.json` as
corpus **`tsc-src`** (`scripts/realworld.py --corpus tsc-src` runs `tttt -b thirdparty/tsc/src`; the checkout already has
`scripts/failed-tests.cjs`; don't run tsc -b in place, it writes `built/local`). Target: the 66 mechanism errors gone,
the 11 checker bugs left (listed above), every project checked once. Scenario tests (see Validation) wired up.
### Stage 2: perf of `-b` (1 session, measured first) — done, see Status
Each program re-reads, parses and binds every referenced file it reaches: compiler's 56 ms parse + 101 ms resolve
names (4 threads, `--time`) is paid in 11 of the 13 programs; checking only a project's own files is already the big
cut (compiler's 290 ms check happens once). Measure `-b src` against `tsc -b` (15.6 s) and tsgo `-b` if available in
`scripts/bench.sh` (tsc -b needs a /tmp copy: it writes outputs). Then pick by the profile, cheapest first:
1. Parallel name resolution (vision's step 8, useful everywhere) shrinks the repeated resolve.
2. Independent projects in parallel: with source redirects no program needs another's result, so `-b` may run
   programs concurrently within the thread budget (tsgo -b builds independent projects in parallel). Memory is the
   limit (testRunner's program peaks at 880 MB): bound it.
3. A process-wide cache of file contents (and line starts) across programs; parsed ASTs only if parsing proves to be
   the cost, and only if parse output doesn't depend on the program (resolve writes into the AST today, so an AST can't
   be shared between programs without the lib program's freeze-and-overlay treatment).
Not before a profile says so: skipping function bodies of reference sources (their return types may be inferred).
### Stage 3: the corpora to zero (1 session, can be a fan-out)
The 11 checker bugs in services/testRunner. A second references corpus that exercises what TypeScript's `src/`
doesn't (package.json `exports`/`types` into `dist/` mapped back through workspace symlinks, nested solution configs,
`${configDir}`, per-package options): **typescript-eslint** is the best candidate seen (root solution config with 19
references, `packages/*/tsconfig.json` solution -> `tsconfig.build.json` (outDir `${configDir}/dist`, rootDir
`${configDir}/src`) + `tsconfig.spec.json`, exports `./dist/*.d.ts`, `workspace:*` deps, a `typecheck:tsgo` script).
Risks to check when pinning: a commit clean under tsc 5.8 (before any TypeScript 6 requirement), generated files
(`packages/types` generates its AST types from `ast-spec`: check they're committed or generate them in the overlay),
and the website packages' dependency weight (an overlay solution config may drop `website`/`website-eslint`). A Vite
template (xstate's `templates/react-ts`) is a cheap smoke test of the common case, not a corpus.

## Validation
- **Scenario tests, mined from tsc's own baselines** (the real oracle; the suite has none): each file under
  `thirdparty/tsc/tests/baselines/reference/tsc/{projectReferences,projectReferencesConfig,redirect,composite}/` and
  `.../tsbuild/*/` starts with `Input::` (every file of a virtual filesystem as `//// [/abs/path]` blocks, including a
  small fake lib at `/home/src/tslibs/TS/Lib/lib.d.ts`), then the command (`/home/src/tslibs/TS/Lib/tsc.js --b
  tsconfig.c.json --listFiles`, `--p reference/tsconfig.json`...) and `Output::` with its diagnostics (pretty-printed:
  strip ANSI, parse `file:line:col - error TSxxxx: message`, or `file(line,col): error ...`). Only the first command
  of each scenario matters (later ones are incremental edits). A small runner (in `baseline_tests` with the mock
  filesystem, or a script writing the tree under /tmp and running `release/tttt`) compares the set of (file, line,
  col, code) with tttt's; `--listFiles` output is not comparable (tsc lists `.d.ts` outputs, we load sources). Expect
  a skip list: scenarios about emit, incremental state, `outFile`/AMD, and `-p` scenarios that expect TS6305 or read
  prebuilt outputs (the divergence above). ~280 tsbuild baselines, most with no diagnostics (a "no false positives"
  oracle); the ones with codes cover TS6306 (69 occurrences), TS6307, TS6053, TS6202, TS6304, TS6059, TS2307 across
  projects, plus TS4094/TS2322/TS2551/TS2396 in referencing projects' own code. Record the pass count in
  `agents/measurements.md`.
- `scripts/realworld.py --corpus tsc-src`: stage 1 leaves exactly the 11 checker bugs, stage 3 reaches 0. Every other corpus
  unchanged (none of them has references in the config it's checked through).
- `scripts/fasttest.sh --summary`: 0 lost. `scripts/threadcheck.sh tsc-src` (if it takes corpora names; else the options) and
  `scripts/checkerdiff.py` byte-identical: the order of projects and their diagnostics must not depend on threads.
- Perf: `scripts/instrcount.sh` unchanged for the existing corpora (the single-project path must not get slower);
  `-b src` time and peak RSS in `measurements.md`, next to `tsc -b`.

## Risks
- **Double counting**: a file reached both as a root and through a redirect, or through a symlink and its realpath,
  must be one `SourceFile` (the loader's canonical path + package_id dedup already exists; the output map must be
  keyed by canonical paths).
- **Diagnostics of reference sources leaking**: name errors are reported in the resolve phase and parse errors while
  loading, before `unchecked` matters. A test: a referenced project with a deliberate error must report it once (in
  its own program under `-b`) and never from the referencing one.
- **Memory under `-b`**: programs are big (400-900 MB each for TypeScript's projects); not freeing them between
  projects would take a 13-project build to ~8 GB.
- **Options confusion**: code that reads `compiler_options()` while loading or resolving a reference source gets the
  program's options where tsc uses the file's (module format, resolution). grep the loader and `module_resolution.c`'s
  callers for it; the checker side stays per program on purpose.
