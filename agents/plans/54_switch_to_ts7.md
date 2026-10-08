# Plan 54: the switch to the TypeScript 7.1 oracle
Written by the prototype session of 2026-10-08 (todo entry 2 of the thirteenth window), plan 53 stage 3/4 made
concrete. Read plan 53 and 53_findings.md first for the why; this file is the how, with the prototype's numbers.

## The pin (done this session)
- **TypeScript `main` at `fed0bf24149fb1ed36039212648bafdafc1ea10e`** = `typescript@7.1.0-dev.20261008.1` (npm's
  `gitHead`). `thirdparty/ensure-typescript.sh` fetches it into `thirdparty/typescript` (depth 1, full checkout:
  460 MB, 350 of it `tsc/testdata`), fetches **Go 1.27.2** (what `tsc/go.mod` asks for; sha256-checked) into
  `thirdparty/go`, module and build caches in `thirdparty/typescript-gopath`, and builds tsgo's CLI from the same
  commit into `thirdparty/typescript/built/tsc` (~90 s cold). `ensure-typescript.sh --env` prints the Go
  environment to `eval`. All three directories are gitignored. The old `thirdparty/tsc` (4d4f005c) stays until
  nothing reads it (stage 3).
- Re-pin to 7.1.0 stable after 2026-11-24: change `TS_COMMIT` to the release's `gitHead`, rerun everything stage 2
  regenerates. One session.

## Tooling decision: Go toolchain + `go build -overlay` (measured, adopted)
tsgo's own Go code is the oracle for everything, built at the pin; tttt's Go files live in our repo and are mapped
into the checkout at build time with `go build -overlay overlay.json` (a JSON map from a path inside the tree to our
file). **The checkout is never modified**, an overlay can also *replace* a tsgo file (a patched copy: this is how we
instrument the oracle now, instead of patching `_tsc.js`), and code in the overlay is inside tsgo's module, so it can
import `internal/...` packages. Both were prototyped:
- **Arbitrary-file baselines** (`tscbaseline.sh`, `filetest.sh`, diffapp's `tsc_custom_baseline.js`, `mutate.py`):
  an overlay test file in package `internal/testrunner` (`TestTtttProbe`: `r := &CompilerBaselineRunner{testSuitName:
  "probe"}; r.runTest(t, f)` for each path in an env var) plus an overlay copy of `internal/testutil/baseline/
  baseline.go` whose `localRoot` comes from an env var. `go test -overlay ... -c` builds a test binary (~6 s warm);
  running it on a file writes exactly the suite's `.errors.txt`/`.types`/`.symbols` (every `@option` configuration)
  in **45 ms**. One process takes many files (mutate's warm-process model). The header line comes out as
  `//// [../../../tmp/.../x.ts] ////`: pass the path relative to `thirdparty/typescript/tsc/testdata`'s parent, or
  rewrite the header (`tests/cases/conformance/x.ts`) after.
- **In-process API** (`$ExpectType`, the DefinitelyTyped and source sweep diagnostics): an overlay `cmd/tttt-oracle/
  main.go` importing `internal/checker` builds and runs. `Checker.GetTypeAtLocation` and `TypeToStringEx(t, nil,
  flags, nil)` exist (`internal/checker/printer.go`), `Program.GetSemanticDiagnostics`, `GetTypeChecker`. Use **one
  checker** (`--singleThreaded` semantics): type ids, hence union order, depend on which checker made a type.
- Rejected: `typescript/unstable/sync` (npm). Unstable by name, a JS wrapper over an IPC'd binary, and it can't be
  patched; the overlay route gives the same calls with none of that.
- Cost: tsgo's CLI checks a DefinitelyTyped package in ~60 ms (40 random ones: 2.4 s, so the whole DT sweep is
  ~3 min on 4 jobs through the CLI alone). Speed is not a reason for an in-process tool any more; `$ExpectType` is.

## The native suite through tttt's harness (prototyped in a worktree, not committed)
Changes needed, each small:
1. **Baselines in subfolders**: `tsc/testdata/baselines/reference/{compiler,conformance}/NAME.ext` (the suite is
   the test file's directory under `tests/cases`). The harness took `compiler/` if the source path contains
   `/cases/compiler/`. Scripts that read `REF` flat (failstats, displaydiff, errtextdiff, jsstats, scenarios,
   mutate, diffapp, findcrash, syntaxstats) need one shared helper. Tests dir layout: `tests/cases` and `tests/lib`
   under `tsc/testdata/tests`, baselines beside `tests` (not inside): `--dir` needs a split or a layout shim.
   The header `//// [tests/cases/compiler/x.ts] ////` is unchanged: keep the tests dir path relative and ending in
   `tests` (an absolute `--dir` printed `/tmp/...` into it).
2. **No `^^^` lines in `.types`.** The prototype stripped them from tttt's output before comparing. Do it properly:
   **delete the type-underline machinery** (`underline_of_type` in `ty_display.c` (44 mentions), `any_underline_at`,
   `any_keeps_underline`, `drop_any_underlines` in `src/test/`, the hacks.md entry "The `any` underline rule reads
   whether tsc's `.errors.txt` exists", plan 07's leftovers). A deletion.
3. **tsgo's skip rules**: `SkipUnsupportedCompilerOptions` (`internal/testutil/harnessutil/harnessutil.go`): skip a
   configuration with module umd/system/amd, moduleResolution node10/classic, esModuleInterop or
   allowSyntheticDefaultImports false, baseUrl, target es5, alwaysStrict false, outFile; and `skippedTests` in
   `internal/testrunner/compiler_runner.go` (the 10 `APISample_*`/`APILibCheck` tests needing `built/typescript.d.ts`,
   plus tests whose options were removed). Tests with baselines are already dropped when the baseline is missing;
   the rule matters for **`@noTypesAndSymbols` tests**, which run errors-only without baselines: 280 such
   configurations reported TS5107/TS5101 deprecation errors and failed in the prototype.
4. **Libs from `tsc/internal/bundled/libs/lib.*.d.ts`** (`refresh.sh`'s `copy_libs_from_tsc`: names lose `lib.`,
   `lib.d.ts` becomes `default.d.ts`, keep the `es7.d.ts` shim). New: es2026 (`es2026.{array,collection,error,full,
   iterator,json,math,typedarrays}`), `esnext.modulesource`, `esnext.promise`; gone: `esnext.{array,collection,
   error,typedarrays}` (now aliases of es2026.*). **Generate tttt's three lib tables** (`lib_names` and `aliases` in
   `compiler_options.c`, `stdlib_priority` in `program.c`) from tsgo's `LibMap` in `internal/tsoptions/
   declarations_generated.go` with a script (the prototype's 20-line python did it; check it in as `src/lib_names.py`
   like `lib_features.py`). Also `SCRIPT_TARGET_ES2026` (target `es2026`, default lib `es2026.full`).
5. **Regenerate `test/baseline_results.ts`** (tsCommit becomes fed0bf24).

## Prototype numbers (the native suite, tttt at 0cb63c56 + the changes above)
12817 test files, 15332 configurations, 13852 ran (1480 have no baselines: tsgo's skipped configurations).
Excluding the 280 skip-rule artifacts: **11842 / 13572 pass (87.3%)**; errors 12584, symbols 13190, types 12865
(components counted over 13852). Today's suite is 14835 / 14969 (99.1%). Of the 1730 failing tests:

| bucket | tests | what |
|---|---|---|
| JS test, baseline changed | 452 | CHANGES.md's JS/JSDoc/CommonJS rework (types: `module.exports` shapes, `this.x` in JS, `typeof A` vs `A` for JSDoc classes; errors: +TS2683/7009/2749/2339/2309, -TS7005 ...) |
| TS test, errors changed | 404 | 171 same codes (text, positions, elaboration); the rest by signature: +TS2741 -TS2345 25, +TS6196 -TS6133 25 (unused type parameters), +TS2300 20, +TS2552 -TS2304 13 (no suggestion limit), +TS2741 -TS2344 13, +TS2303 11 (circular imports), +TS2304 6, -TS2497 6, node16 import-attribute/mode codes (TS1453-1464, TS2823/2856) ~16, +TS2740 -TS2345 5, ... |
| new test (in tsgo's tree, not 4d4f005c's) | 281 | ~560 new test files incl. 7.1's features (source phase imports, ambient-module attributes, es2026, regex target gating) and tsgo's own regression tests |
| TS test, types/symbols changed | 302 | union order (the 7.1 ordering fixes), `new A` of an abstract or private-constructor class is `A` not `any`, computed names kept (`{ [Type.A]: ... }` not `{ 0: ... }`), import aliases print as the local name (`typeof beez`, `Symbol(beez, ...)` not `provide.bar`), lib symbols list each declaration once (`Array.splice`: 2 `Decl`s, 4 before) |
| types: quote style only | 109 | tsgo reuses the source's quotes in printed literal types (`'hi'` where 6.0 printed `"hi"`): one printer rule |
| failed before | 122 | the old failures (134 today) |
| errors: option diagnostics only | 50 | TS5101->TS5102, TS5107->TS5108, TS5102->TS5023, TS6046 (plan 53 stage 2) |
| API tests | 10 | tsgo skips them (item 3) |

Also: symbols components fail in 661 tests, 33 of which are only the lib declaration count above.

**7 new tests crash tttt** (segfault): forOfSelfReferentialDottedName, templateLiteralTypeExcessiveLength,
unreachableFlowAfterThrowingFor{Head1,Head2,InOfHead1,InOfHead2}, variadicTupleMismatch. **intersectionConstructor
ReductionCrash takes 63 s** (passes), declarationEmitOversizedCachedType 4.5 s: the native suite took 80 s against
today's 11 s, almost all in that one test. `agents/bugs/ts7_new_tests.md` has them. The suite can't be the default
until these are fixed (stage 1).

## Stages
### Stage 1: the switch (1 session)
- `thirdparty/ensure-tsc.sh`'s callers (fasttest.sh, test.sh, ...) call `ensure-typescript.sh`; `baseline_tests`'
  default tests dir is the native tree (items 1-3 above). `refresh.sh` copies the libs from the new tree, the lib
  tables are generated (item 4), es2026 target.
- Fix the 7 crashes and the 63 s test (they are tttt bugs: the tests are tsgo's regressions for the same shapes).
- Delete the types underline machinery (item 2).
- The suite scripts read the subfolder layout through one helper: failstats, displaydiff, errtextdiff, jsstats,
  syntaxstats, findcrash, resultdiff/passcount (results file), diffapp.sh.
- Regenerate `test/baseline_results.ts`: expect **~11850 / 13570** (87%). Lost against today: by definition
  everything in the table above; `scripts/resultdiff.py` compares against HEAD's saved results, so this session's
  commit message carries the bucket table instead.
- Validate: `scripts/fasttest.sh --summary` clean on a rerun, no crash under `findcrash.py`, suite wall time back to
  ~11 s, `TTTT_TS7`-style hacks absent (the prototype's are not to be copied).

### Stage 2: the oracle tools on tsgo (1 session)
- `test/oracle/` (Go, our code): the probe test (arbitrary-file baselines) and `tttt-oracle` (diagnostics per
  project, `$ExpectType`), built by `ensure-typescript.sh` with an overlay map generated from `test/oracle/`.
  `tscbaseline.sh`, `filetest.sh`, diffapp and `mutate.py` move to the probe.
- `scripts/sweep.py`'s and `expecttypes.py`'s oracle (`test/sweep_tsc.js`) and `sourcesweep.py`'s move to
  `tttt-oracle`. Decide here: **CLI semantics everywhere** (what `tsc -p P --noEmit` prints: stop at option errors,
  noEmit implied). That fixes the source sweep's 26% fidelity gap and removes `--as-emitting`. If a project's
  diagnostics depend on emit-only options (TS5011/5055/6059), they leave the caches and tttt's CLI never reports
  them (consistent with the drop-in definition: we check, tsc --noEmit checks).
- Validate: the probe reproduces the checked-in baselines for 50 random suite tests byte for byte; `tttt-oracle`'s
  diagnostics equal `built/tsc -p P --noEmit --pretty false` on 30 DT packages and 30 source sweep projects.

### Stage 3: regenerate every cache (1 session)
- `test/sweep/` (DT diagnostics, nuia/eopt flag variants, `$ExpectType`), `test/sourcesweep/`, `test/flagflip/`,
  mutate's cache, the corpora's expectations: `realworld.py` corpora that tsgo 7.1 isn't clean on get their
  expected diagnostics recorded or are re-pinned (webpack's JS: 602 under 7.0.2; drizzle 16, vue 10, effect 3 are
  TS2769/TS2578 pairs; zod-root ran away under 7.0.2: retry under 7.1, single-threaded, `ulimit -v`).
- `scenarios.py`: tsgo's `tsc/testdata/baselines/reference/tsbuild` and `tsc` replace 4d4f005c's (another format:
  look before porting; keep the 6.0 ones until then if it's more than half a session).
- `bench.sh`/`instrcount.sh`: `thirdparty/tsc/src/compiler` is a corpus there. Replace it with tsgo's own sources?
  No, those are Go. Keep TypeScript 6's `src/compiler` as a pinned **corpus** in `ensure-corpora.sh` (it is the
  biggest pure-TS project we have, and it doesn't care which oracle we have), not as the oracle checkout.
- Ports of the scripts that read tsc's JS sources: `src/lib_features.py` (getScriptTargetFeatures),
  `src/unicode_identifiers.py`, `src/regex_tables.py`, `scripts/unimplemented_codes.sh` (diagnosticMessages.json:
  `tsc/internal/diagnostics/diagnosticMessages.json`), `scripts/syntaxstats.py`, `scripts/clidiff.py`'s TSC6.
- Then **delete `thirdparty/tsc`** from ensure-tsc.sh / refresh.sh (ensure-tsc.sh goes away), documentation.md's
  "baseline_tests" oracle paragraph and CLAUDE.md's oracle paragraph rewritten, measurements.md's numbers replaced.
- Validate: `scripts/oracles.py` runs every oracle against the new caches with nothing reading `thirdparty/tsc`
  (`grep -rn thirdparty/tsc scripts src test` is empty apart from the corpus).

### Stage 4: delete what 7 deleted (1 session)
Plan 53 stage 2: 7.0's option errors (TS5108 for es5/amd/umd/system/node10/classic/esModuleInterop=false/...,
TS5102 for baseUrl/outFile/downlevelIteration, TS5023 for 6.0's TS5102 set, TS6046 for es3/none, TS5095,
`ignoreDeprecations: "6.0"` silences nothing), then delete the code paths behind them (es5 lowering checks,
amd/system/umd module kinds, node10/classic resolution, baseUrl/paths-without-baseUrl stays, outFile). 50 tests
are only the option codes. Suite: 0 lost except tests that passed only through deleted paths (count them).

### Stage 5+: the deltas (fan-outs, 3 sessions)
- **TS checker deltas** (fan-out, 4 sub-agents by signature): the 404 errors + 302 types/symbols + 109 quote-style
  tests above. Mechanisms, biggest first: elaboration codes (TS2345/2344 -> TS2741/2740/2739, TS2696 -> TS2322),
  unused type parameters TS6196, duplicate identifiers TS2300 (+20), the suggestion limit (TS2552), circular imports
  TS2303, source quote style in the printer, alias symbol naming, lib declaration lists, the 7.1 ordering fixes in
  `ty/order.c`, `new` of abstract classes. Same-code text diffs (171) through errtextdiff.
- **JS rework** (fan-out, 4 sub-agents by CHANGES.md table): 452 tests. Expect deletions (constructor functions,
  prototype assignment, `@class`/`@constructor`/`@enum`, value-as-type, `this`/`module.exports` aliases, nested
  expandos) before additions; jsstats.py on the new tree sizes each.
- **7.1 language and the new tests** (281): source phase imports, ambient-module import attributes (TS1550-1558),
  target-gated regex (TS18062/18063), TS5113-5115/18064/18116/18117, noUnusedLocals in static blocks.

## Validation (the whole plan)
- The native suite is the default suite; `scripts/oracles.py` runs every oracle against tsgo-generated caches;
  `thirdparty/tsc` is gone; documentation.md, CLAUDE.md and measurements.md describe the 7.1 oracle.
- Suite back above 95% by the end of the window's fan-outs (the 6.0 suite took plan 48 one window to recover).
