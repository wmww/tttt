# Plan 53: TypeScript 7 as the target, and tttt as a drop-in `tsc`
Written by the triage session of 2026-10-08 (todo entry 10 of the twelfth window) on the human's request in
triage.md: "ideally I'd like this project to be a drop-in replacement for the latest tsc/tsgo (thus implement
typescript 7). make sure it implements and is tested against the latest features of the language." The research is
in `53_findings.md` (a sub-agent's measurements: read it before executing any stage). **The next insights session
schedules this**; stages 1-2 can start right away, stages 3-5 are one plan-48-sized window.

## Facts that decide the shape (2026-10-08, details in 53_findings.md)
- **There is no JS TypeScript after 6.0.** typescript-go was folded into `microsoft/TypeScript` `main` under `tsc/`
  (2026-08-20); the JS `src/` is gone, and 4d4f005c (our oracle) is the last JS compiler. The 6.0 suite was promoted
  into `tsc/testdata/tests/cases` (12.9k files) with native baselines (no `.diff`s, no `^^^` lines in `.types`).
- **Latest stable is 7.0.2** (`tsc` on PATH). 7.1 branched 2026-07-07: beta 10-06, RC 11-10, **stable 11-24**.
- **On TS code, 7.0 ≈ our oracle**: 10 of 13 corpora identical; 76% of the source-sweep sample identical, 18.5% are
  configs both reject (6.0 deprecation vs 7.0 removal), 4% differ (TS2304 -> TS2552 without the 10-suggestion
  limit, duplicated @types packages / `deduplicatePackages`, TS2345 -> TS2741/2740 elaboration, TS6305 under
  references, TS2769/TS2578 overload pairs). What 7.0 changes is **options** (6.0's deprecations are hard errors that
  stop checking; `ignoreDeprecations` no longer helps), **JS/JSDoc/CommonJS** (large removals: webpack goes 0 -> 602)
  and a few rules (no suggestion limit, template literal inference by code point, `strict: false` optional params).
- **7.1's language delta is small**: source phase imports (`import source x`, `import.source()`), import attributes
  on ambient modules (`declare module "*.css" with {...}`), target-gated regex syntax, ~8 new diagnostics, the es2026
  target/lib, `Promise.allKeyed`, a DOM update, and ordering fixes that touch our stable-ordering port.
- **tsgo's baselines are a good oracle now** (plan 48's "poor oracle" was relative to a 6.0 target): against ours
  errors 91.5%, types 92.9% (carets dropped), symbols 96.1% identical; 7.0 -> 7.1 drift ~2%. 1861 configurations
  are gone (the removed options).
- **tsgo is order dependent too**: vue's vModel.ts:446 TS2345 appears under tsgo at every checker count
  (agents/bugs/include_order.md), so a 7.x vue corpus expects it, the opposite of tttt's walk-order HACK.

## Decisions (proposed; insights confirms)
1. **Target 7.1.0 stable** (2026-11-24) for the oracle switch, same commit everywhere (suite, caches, binary). If
   the switch starts before then, pin 7.0.2 / its TypeScript commit and re-pin 7.1 later (~2% churn).
2. **Drop-in means**: every invocation and tsconfig tsc 7 accepts for checking (`-p`, `-b`, files, flags) is
   accepted; diagnostics in tsc's formats (`--pretty false` byte-identical, pretty close); tsc's exit codes. **Emit
   stays out of scope** but is the real gap for "drop-in" (most users run tsc to emit): say so in readme.md, and
   insights should ask the human whether `.d.ts`/JS emit enters scope after the retarget.
3. **Delete what 7 deleted** with the switch, plan-48 style (no dual 6/7 mode): es5/es3, amd/umd/system/none,
   node10/classic, baseUrl, outFile, downlevelIteration, esModuleInterop/allowSyntheticDefaultImports/alwaysStrict
   false, and the JS features CHANGES.md lists as removed. Expect a net code deletion in the JS front end.

## Stages
### Stage 1: CLI parity (1 session, independent of the target, can start now)
tttt today prints coloured progress ("Loading libraries..."), `file:line [TSxxxx] message` plus a source excerpt and
its own summary, exits 1 on errors (tsc 6/7 exit 2 with `--noEmit` errors, 1 for some config errors: check
`ExitStatus`), stops at 5000 diagnostics per phase (`--all-errors`), names its options `--threads`/`--build-jobs`.
- tsc's `--pretty false` format (`file(line,col): error TSxxxx: message` + indented chains + related info), the
  pretty format (codeframes, "Found N errors in M files.", default when stdout is a TTY), exit codes, no chatter.
- tsgo's flags: `--checkers`, `--builders`, `--singleThreaded`, `--quiet`, plus `--version`, `--listFiles`,
  `--listFilesOnly`, `--explainFiles`, `--showConfig` where cheap. Keep tttt's own debugging flags.
- Every script parsing tttt's output (realworld.py, sweep.py, sourcesweep.py, flagflip.py, checkerdiff.py,
  threadcheck.sh, oracles.py...) moves to the tsc format in the same session (or tttt keeps a `--format tttt`).
- Validate: byte-diff of `tttt --pretty false` against `tsc --noEmit --pretty false` on every corpus and a
  source-sweep sample, exit codes included.

### Stage 2: 7.0 option semantics (1 session; lands with stage 4 if insights prefers one switch)
Removed options become 7.0's errors that stop checking (TS5108, TS5102, TS5023 for 6.0's TS5102 set, TS6046 for
es3/none), `ignoreDeprecations: "6.0"` silences nothing, TS5095 for amd/umd/system under bundler. Then delete the
code paths behind them (es5 lowering checks, amd/system module kinds, node10/classic resolution, baseUrl/outFile).

### Stage 3: oracle switch (plan-48-sized: prototype, switch, fan-out, oracles, corpora, perf: ~1 window)
- `thirdparty/ensure-tsc.sh` fetches the pinned TypeScript `main` commit sparsely (`tsc/testdata`, the bundled libs,
  `tsc/CHANGES.md`) plus the matching npm `typescript@7.1.x`. Libs from the 7.x tree.
- Harness: the new test tree, no caret lines, the skip list; test names keep `name(option=value)`.
- Caches regenerated with tsgo: corpora, flagflip, scenarios and the source sweep through the CLI (which also fixes
  the source sweep cache's fidelity problem: 53_findings.md §1, it isn't what `tsc -p --noEmit` prints in 26% of
  projects); the DefinitelyTyped sweep and `$ExpectType` through `typescript/unstable/sync` (`getTypeAtLocation`,
  `typeToString`) or the CLI per package. Pass `--tsBuildInfoFile` into /tmp (both tscs write `.tsbuildinfo` into
  thirdparty/sources even with `--noEmit`).
- `tscbaseline.sh`/`filetest.sh`/`mutate.py` need baselines for arbitrary files: tsgo's Go test runner (install a
  Go toolchain, build `tsc/` at the pinned commit) or a CLI+API substitute. Decide in the prototype.
- Re-pin corpora 7 disagrees on (webpack's JS: 602; drizzle 16, vue 10, effect 3 are mostly TS2769/TS2578 pairs).
- Expect the suite to fall to ~90% at the switch (JS-heavy), then recover by fan-out on the 7.x deltas: the
  suggestion limit, code-point template inference, the `strict: false` parameter rule, elaboration codes, package
  dedup, TS6305 under references, and the JS/JSDoc/CommonJS rework (CHANGES.md tables; ~1400 accepted-diff
  baselines, mostly deletions in tttt).

### Stage 4: 7.1 language (1-2 sessions, after stage 3; its tests are in the native suite)
Source phase imports, import attributes on ambient modules (TS1550-1558), target-gated regex syntax
(TS18062/18063), TS5113/5114/5115/18064/18116/18117, `noUnusedLocals` in static blocks, es2026 lib, the ordering
fixes (bigint literals, same-named aliases, anonymous/reverse mapped types, variance order, `infer` order).
`contentMappers` (.vue/.svelte plugins running external code) is out of scope unless the human says otherwise.

## Validation
- Stage 1: `tttt --pretty false` byte-identical to tsc's on every corpus and the source-sweep sample, same exit
  codes; every script still works.
- Stage 3: the native suite runs through `scripts/fasttest.sh`; every oracle regenerated against the pinned 7.x;
  `agents/measurements.md` has the new numbers and documentation.md the new oracle decisions; 4d4f005c is gone from
  ensure-tsc.sh.
- Stage 4: the 7.1 feature tests pass; a probe file per syntax feature matches `tsc` 7.1.
