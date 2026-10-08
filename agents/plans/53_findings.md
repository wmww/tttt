# TypeScript 7 retarget: findings (agent td, 2026-10-08)

Research by a triage sub-agent (2026-10-08) for plan 53; raw data was in /tmp/td (not kept). Numbers are from that day: tsc 7.0.2 (npm latest), 7.1.0-dev.20261008.1, TypeScript main c0ca4744c9.

## 0. Headline: where TypeScript is now
- **typescript-go no longer exists as a separate repository.** It was closed on 2026-08-20 and its history was imported into `microsoft/TypeScript` `main`, under `tsc/` (commit 5f647a8, "Apply the TypeScript 7 repository layout").
- **The JS compiler is frozen.** `main` no longer has the JS `src/`. Since our oracle 4d4f005c, only one JS change landed (b465fdb, a lib signature).
- **7.1's tests are native.** The old `tests/cases` were "promoted" into `tsc/testdata/tests/cases`: 12,851 files (6,891 compiler, 5,935 conformance, 25 transpile). That includes tsgo's own ~317 tests; colliding names are listed in `promotedTestCollisions.txt`.
  - Baselines are in `tsc/testdata/baselines/reference/{compiler,conformance}`: 7,386 `.errors.txt`, 12,870 `.types`, 12,870 `.symbols`. There are no `.diff` files any more.
- **Schedule:** 7.0.2 is the npm `latest`. 7.1 branched at a5a219c3b5 (2026-07-07), has 415 commits so far, beta 10-06, RC 11-10, stable 11-24 (iteration plan microsoft/TypeScript#63703).
- **What this means for us:** 4d4f005c is the last JS oracle there will be. Any "TypeScript 7" target has to take the Go repository's baselines and binaries as its oracle.

## 1. What a tsc 7.0.2 user sees that differs from 4d4f005c (6.0.3 + ordering)

### Options
- **6.0's deprecations are now hard errors.** Each stops the program before checking, and `ignoreDeprecations: "6.0"` no longer helps. Probed on the CLI:

  | option | 6.0 | 7.0 |
  |---|---|---|
  | target es5 | TS5107 | TS5108 "has been removed" |
  | target es3 | TS5108 | TS6046 (not a valid value) |
  | module amd/umd/system | TS5107 | TS5108, plus TS5095 when moduleResolution is the bundler default |
  | module none | TS5107 | TS6046 |
  | moduleResolution node/node10/classic | TS5107 | TS5108 |
  | baseUrl, outFile, downlevelIteration | TS5101 | TS5102 |
  | esModuleInterop / allowSyntheticDefaultImports / alwaysStrict = false | TS5107 | TS5108 |
  | options 6.0 already rejected with TS5102 (charset, out, keyofStringsOnly, suppressImplicitAnyIndexErrors, noImplicitUseStrict, importsNotUsedAsValues, preserveValueImports, noStrictGenericChecks, suppressExcessPropertyErrors) | TS5102 | TS5023 "Unknown compiler option" |

- **New CLI flags:** `--checkers`, `--builders`, `--singleThreaded`, `--quiet`/`-q`, `--pprofDir`, `--watchInterval`, and `--deduplicatePackages` (default true). `--stableTypeOrdering` is listed but always on.
- **Unchanged:** TS5112 for files on the command line next to a tsconfig (already in 6.0, `--ignoreConfig` skips it).
- **Defaults:** the same as 6.0's (strict, module esnext/bundler, `types: []`, noUncheckedSideEffectImports, libReplacement false, rootDir `./`). tttt already has all of them.

### Checker
- **Intended to be identical to 6.0** for clean, non-deprecated TS code, per the 7.0 announcement. The measurements below agree.
- **Known intentional differences** (from `tsc/CHANGES.md`):
  - The 10-suggestion limit is gone: "Did you mean" (TS2552) is always offered where 6.0 stops after 10 (probed: the 14th unresolved name gets TS2552 in 7.0, TS2304 in 6.0).
  - Template-literal inference splits by code point instead of UTF-16 unit.
  - With `strict: false` you can no longer omit `undefined`/`unknown`/`any` parameters.
  - Node positions are UTF-8 offsets. That affects the API; CLI columns look the same.
- **JS / JSDoc / CommonJS is the big rework** (CHANGES.md has the tables). Removed:
  - Constructor functions and prototype assignment; `@class`, `@constructor` and `@enum`.
  - The `?` type, Closure function types and namepaths.
  - Values used as types in JSDoc (now TS2749).
  - `this` and `module.exports` aliases, nested undeclared expandos and `f.x = f.x || init`.
  - Single-property `require('x').y`, and mixing `module.exports =` with `exports.x`.

  Changed:
  - CommonJS exports assigned in branches now union.
  - `@param` applies to one function only.
  - `@typedef` in a class is hoisted.
  - `@type` assertions block narrowing.
  - `arguments` no longer implies a rest parameter.
- **Message texts:** very few changes (TS18061 mentions `source` in 7.1, TS5102/5108 are reused). Most `.errors.txt` differences are code or set changes, not wording.
- **Unsure, worth a quick check:** tsgo does not report TS6305 ("output file not built from source") where tsc 6 does. In base-ui/packages/utils, 6.0 prints 279 lines of it and 7.0 prints nothing, so 7.0 seems to treat referenced sources more like tttt does.

### Measured on the pinned corpora (`-p`, `--noEmit`, 4d4f005c tsc.js vs tsc 7.0.2)

| corpus | tsc 6 (4d4f005c) | tsc 7.0.2 | tsgo's extra |
|---|---|---|---|
| tsc src/compiler, type-fest, valibot, react, nest, zod, xstate, excalidraw, tanstack-router | 0 | 0 | none |
| webpack (checkJs) | 0 | **602**, all in .js | TS2749 x210 (JSDoc value-as-type), TS2339 x78, TS7053 x71, TS2538 x59, TS2345 x53, TS2565 x22 |
| drizzle | 0 | 16 | TS2769 x8 + TS2578 x8 pairs (overload resolution) |
| vue | 0 | 10 | TS2769 x4 + TS2578 x4, TS2345, TS2322 |
| effect (v3) | 0 | 3 | TS2769 x2, TS2345 |

- Not run: vscode, zod-root (tsgo runs away on it), tsc-src and typescript-eslint (`-b` writes buildinfo). ensure-corpora.sh records vscode as clean under both.
- Wall time on a loaded 4-core machine: tsgo is 3-12x faster than tsc.js (e.g. tsc compiler 14.8 s vs 131.7 s). type-fest took 113 s and 6.0 GB under tsgo against 174 s under tsc.js.
- **Conclusion for TS:** 10 of 13 corpora are identical. The rest is overload-resolution corner cases (TS2769/TS2578 pairs, which ensure-corpora.sh already calls tsgo bugs or differences) plus the JS rework.

### Measured on the source sweep (quarter sample, `--sample 4`: 822 projects in 119 repos, both via the CLI with `--noEmit`)

| verdict | projects |
|---|---|
| identical (file, line, col, code) | **622 (75.8%)**; 53 repos fully identical |
| tsc 6 already stopped on a deprecation error (TS5101/5107); 7.0 stops on TS5102/5108 | 152 (18.5%) |
| 6.0 accepted the config (`ignoreDeprecations: "6.0"`), 7.0 rejects it | 14: baseUrl 8, node10 5, downlevelIteration 2, system, outFile, esModuleInterop=false, suppressImplicitAnyIndexErrors |
| diagnostics differ | 33 (4.0%) |
| timeout | 1 |

- **Repos:** 40 of the 119 have at least one project that 7.0 refuses to check because of its config. Those configs are the biggest user-visible break.
- **The 33 differing projects:**
  - TS2304 -> TS2552 in 7 projects (the suggestion limit).
  - Duplicate-declaration families (TS2300/TS2320/TS2430) in about 6 projects (nuxt, trpc/examples/nuxt, dyad, quill: @types duplicated across node_modules, probably `deduplicatePackages` / package dedup).
  - TS2345 -> TS2741/TS2740 in 4 projects: a different elaboration code (lit, qwik, better-auth).
  - TS6305/TS6310 project-reference handling in 5.
  - redux-toolkit infinite-queries: 7.0 adds 65 TS7006 where 6.0 stopped at TS6305.
  - TS2769/TS2578 pairs in 3, TS6133 vs TS6196 in 1, single TS2322s in 3.
- **Wall time:** summed per-project time was 12,818 s for tsc 6 vs 2,623 s for tsgo, both at 2 jobs on a loaded machine.
- **Side finding about our own oracle:** `test/sourcesweep/tsc.jsonl.gz` (written by `test/sweep_tsc.js`) is not what `tsc -p --noEmit` prints in 214 of 821 projects (26%):
  - 85+ projects: the CLI stops at options/global errors (TS5101/5107/TS2688...) before checking. The cache checks anyway, e.g. graphql-yoga/examples/bun is 1 diagnostic on the CLI and 4,133 in the cache.
  - 30 projects: emit-only option errors (TS5011/5055/6059) appear because the cache doesn't pass noEmit.

  Whatever tttt's CLI does here, the cache isn't "what tsc prints". Worth a triage note.

## 2. What is new in 7.1 (dev) as language
Source: a sub-agent read the 7.1 range a5a219c3b5..c0ca4744c9 and probed `typescript@7.1.0-dev.20261008.1`. **[V]** = probed, **[C]** = seen in commits.

### (a) Syntax
- **Source phase imports** [V], 2f9fd09 #63915: `import source x from "m"` and `import.source(...)`, with TS18111-18115 and `lib.esnext.modulesource` (`AbstractModuleSource`).
- **Import attributes on ambient modules** [V], 253c5e2 #63931: `declare module "*.css" with { type: "css" } {}`.
  - The most specific match wins.
  - New errors TS1550-1558; TS1463/1464 are gone.
  - Follow-ups: no `readonly` (ee72eb0), no template literals (792ffcc), string literals required in import-type attributes (6ed67b6).
- **Regex syntax gated by target** [V], cb38098: TS18062 for modifiers (needs es2025), TS18063 for duplicate named groups.
- **Parser fixes** [C]: `as`/`satisfies` between `**` operators, and dotted private names in `typeof`.

### (b) Checks and inference
- **New diagnostics:**
  - TS5113 "only assignable to the non-distributed ..." [V].
  - TS5114/5115 "instantiations appear infinitely circular", replacing TS2589 in some cases [C].
  - TS18116 "did you mean `await using`" [V].
  - TS18064: private identifiers in destructuring [V].
  - TS18117: a deferred import needs a namespace binding [C].
- **Changed checks:**
  - `noUnusedLocals` now covers class static blocks [V].
  - TS2857 is dropped [V].
  - TS9019 is narrowed [V].
  - Computed property names are always checked [C].
- **Inference and relation fixes** [C], each small: binding-pattern defaults, recursive call resolution, a cache for inferences from type arguments, static properties no longer contextually typed by their own class, never-reduction of intersections, a cap on template-literal size, TS2454/TS2871 false positives.
- **Type-ordering fixes** [C], which directly affect our stable-ordering port: bigint literals, same-named aliases, anonymous and reverse mapped types (454c081), variance computation order (d20f192), stable `infer` type-parameter order (50d70a3).

### (c) Lib
- `es2026` target and lib (es2026.array/collection/error/typedarrays moved out of esnext; `JSON.rawJSON`, `Math.sumPrecise`, `Iterator.concat`).
- `Promise.allKeyed` / `allSettledKeyed`.
- A DOM update of about 1.9k lines.

### (d) Options
- `es2026`.
- `contentMappers` plus `--runExternalCode`: plugins that turn .vue/.svelte files into TS (TS18065-18110). This is tooling-like, but it changes which files get checked.
- Stricter tsconfig validation: misplaced root options, spelling suggestions, references fields.

### (e) Tooling only
- The stable programmatic API: the headline of 7.1. `typescript/unstable/sync` already exposes `getSemanticDiagnostics`, `getTypeAtLocation` and `typeToString`.
- LSP, watch, `-b` scheduling, perf, the repository migration.

**Net:** 7.1 is a small language delta: two syntax features, about 8 new diagnostics, es2026 lib, and ordering fixes. 7.0 itself has no new TS syntax over 6.0.

## 3. Oracle options for 7.x

### Is tsgo's suite still a poor exact-match oracle? Not any more.
The plan-48 verdict was relative to a 6.0 target. Measured now against 4d4f005c's baselines:
- **7.0 (ts7-release, `submodule/` baselines):**
  - `.errors.txt`: 6,316 of 6,906 shared files identical (91.5%).
  - `.types`: 11,288 of 12,155 identical once the `^^^` lines are dropped (92.9%).
  - `.symbols`: 11,678 of 12,155 identical (96.1%).
  - 1,861 configurations are skipped (es5, amd/umd/system, node10/classic, esModuleInterop/allowSyntheticDefaultImports false, baseUrl, outFile, alwaysStrict false), plus 47 named tests whose options are gone.
  - Of the 717 differing `.errors.txt`, 503 have a different diagnostic set. The top codes are TS2304->TS2552 (198 diagnostics in 21 files), TS7005 98 (one test), TS2683/7008/7009/2749/2339 (JS), and TS2880/TS2307 (resolution). 214 differ only in text or elaboration.
  - 1,426 accepted-difference entries, mostly JSDoc/JS, and 87 "triaged" ones (bugs tsgo intends to fix).
- **7.1 main** (10% sample of 3,189 baselines):
  - Against our 6.0 oracle: errors 89.6% identical (+2.1% same diagnostic set), types 92.6% after normalization, symbols 94.5%.
  - Against 7.0's baselines: **98% identical**. Drift between 7.0 and 7.1 is about 2%.
- **Format differences** are harness-only:
  - `.types` without the `^^^` lines.
  - Some symbols differ because the lib differs (e.g. `Array.splice` has 2 declarations in 7.1's lib, 4 in ours).
  - Plan 48 also mentioned source quote style; I didn't see that in this sample.

### The options

**A. Native suite (TypeScript `main`'s `tsc/testdata`) as the oracle.** This is the only first-party 7.x suite there will be. tsgo's runner reads the same directive format (`// @target:` etc.), and test names keep the `name(option=value)` form.
- Harness changes:
  - Read the new tree.
  - Drop the caret lines (or tsgo writes none, so just don't print them).
  - Skip the unsupported configs.
  - Use 7.x libs (from `tsc/internal/bundled/libs`, or similar).
- **`tscbaseline.sh`/`filetest.sh`/`mutate.py` lose their JS harness.** They need tsgo's Go test runner on an arbitrary file, which means installing a Go toolchain (none on the machine) and building `tsc/` from the pinned commit. Medium cost: the runner already takes a test file and writes baselines.

**B. Hybrid:** the 6.0 suite for what 7 kept, tsgo baselines for what changed.
- No longer needed: in (A) the suite *is* the 6.0 suite plus 7's changes.
- Only useful as a transition: keep 4d4f005c for the oracles that need the JS API (see C) until they are ported.

**C. tsgo on the corpora and sweeps** (fast: 5-9x).
- Easy to swap:
  - flagflip, realworld/corpora and scenarios.py: CLI only.
  - sourcesweep: switch to the CLI, which also fixes the cache-fidelity issue above.
  - DefinitelyTyped sweep: `sweep_tsc.js` uses the in-process API with a lib cache. Use tsgo's `unstable/sync` API, or just the CLI per package with tsgo's speed.
- **expecttypes:** needs `getTypeAtLocation` + `typeToString` at spans. 7.0.2 and 7.1 ship `typescript/unstable/sync` with both. Port `sweep_tsc.js --expect` to it; this is a prototype question, since the API is "unstable" (and goes beta in 7.1).
- **Cache regeneration:** all caches regenerated once, with tsgo pinned by version (npm `typescript@7.x.y` or a Go build at a commit).

### Recommended oracle
**(A) + (C) at a pinned 7.x commit**, the same commit everywhere. 7.1.0 stable (2026-11-24) is the natural pin; use 7.0.2 if starting earlier and re-pin 7.1 later, since drift is about 2%.

## 4. Recommendation: what "drop-in for tsc 7" should mean, staged

**Definition:** `tttt` accepts every invocation and tsconfig that `tsc` 7.x accepts for checking (`-p`, `-b`, files, flags), prints the same diagnostics in tsc's formats (`--pretty false` byte-identical; pretty close), and returns the same exit codes. Emit stays out of scope: `--noEmit` semantics, and say so; this is the real gap for "drop-in", since most users run `tsc` to emit.

**Today's CLI gaps found in passing:**
- tttt's output format isn't tsc's: it prints coloured "Loading ..." progress and a `file:line [TSxxxx] message` form.
- It exits 0 with errors (tsc exits 1 or 2).
- It caps output at 5,000 diagnostics per phase.
- These matter more for drop-in than any 7.1 feature.

**Stage 1: CLI parity** (1 session, cheap, independent of the target).
- tsc's `--pretty false` format, and the pretty format with codeframes and "Found N errors in M files."
- Exit codes.
- No progress chatter by default.
- `--checkers`/`--builders`/`--singleThreaded`/`-q` spelled like tsgo's (tttt has `--checkers`, `--threads`, `--build-jobs`).
- `--version`, `--showConfig`, `--listFiles`/`--explainFiles` if cheap.
- Validate: byte-diff against `tsc --noEmit --pretty false` on the corpora and the source sweep.

**Stage 2: 7.0 option semantics** (1 session, cheap).
- Removed options become TS5102/TS5108/TS5023/TS6046 errors that stop checking.
- `ignoreDeprecations: "6.0"` no longer silences them.
- es3/none become TS6046.
- Behind the target version switch (or simply the new default once the oracle moves).
- **Decide whether to keep the es5/amd/node10/baseUrl code paths at all.** 7.x users can't reach them. Deleting them is a simplification win, but it loses 1,861 suite configs that pass today. Recommendation: delete them with the retarget; tsgo did the same.

**Stage 3: the 7.x semantic deltas** (2-3 sessions, fan-out).
- No suggestion limit (TS2552): trivial.
- Template-literal code-point inference: small.
- `strict: false` optional-parameter rule: small.
- The JS/JSDoc/CommonJS rework (CHANGES.md tables): the big one. tttt's JSDoc+CommonJS+expandos machinery (plan 26, plan 50) mostly *shrinks*: no constructor functions, prototype assignment, `@enum`/`@class`, value-as-type, `this` aliases or nested expandos. Probably a net deletion of code, but about 1,400 accepted-diff baselines to re-match. Size it with a prototype run of the native suite first.
- tsgo's elaboration-code changes (TS2345 -> TS2741/2740), the duplicate-package/dedup behaviour, TS6305 under references.

**Stage 4: oracle switch** (plan-48-sized, about one window).
- Pin a TypeScript `main` commit (7.1.0 when stable).
- `ensure-tsc.sh` fetches it (sparse: `tsc/testdata`, `tsc/internal/bundled` libs, `tsc/CHANGES.md`) plus the matching npm `typescript@7.1.x` binary.
- Port the harness (option A); regenerate every cache with tsgo (option C); port `sweep_tsc.js --expect` to `unstable/sync`.
- Install Go to build tsgo's test runner for `tscbaseline.sh`/`filetest.sh`/`mutate.py`, or replace them with the CLI plus the API.
- Re-pin corpora that 7.0 rejects or disagrees on (webpack's JS is the big one: 602).
- Same structure as plan 48 (prototype -> switch -> fan-out -> oracles -> corpora -> perf). Expect the suite to drop to roughly 90% on the switch (the ~8-10% of baselines 7 changed, concentrated in JS), then recover.

**Stage 5: 7.1 language** (1-2 sessions).
- Source phase imports, import attributes on ambient modules, es2026 lib, about 8 new diagnostics, the ordering fixes.
- These come free with the stage 4 oracle (their tests are in the native suite).

**Not worth doing:** a dual 6/7 mode. The JS compiler is frozen and tsgo deletes the old options, so tttt should follow, as plan 48 did for 5.8.

**Rough total:** about 1 window (6-9 sessions). Stages 1-2 can start now on the 6.0 oracle. Stages 3-5 wait for a 7.x pin (7.1.0 stable on 2026-11-24 is 7 weeks out; 7.0.2 works now with about 2% re-pin churn later).

Housekeeping: both CLIs with `--noEmit` still wrote 161 `.tsbuildinfo` files into `thirdparty/sources`, in projects with `incremental` or `composite`. None were tracked, and all are deleted. Future CLI oracles should pass `--tsBuildInfoFile` into /tmp, or clean up afterwards. /tmp/td is 117M (the `typescript@next` install and the blob-less clone in /tmp/td/q2).
