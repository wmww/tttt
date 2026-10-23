# Todo
The live list of directions and ideas, roughly in order (vision.md "Direction" says why). Each entry: what, why, what
is known, where the numbers are. Delete an entry when it lands, leaving its numbers in measurements.md; add new ones
where they fit the order. Uninvestigated items go to triage.md, known bugs to `bugs/*.md`.

Starting point: measurements.md "Oracle totals (2026-10-23, a5252a764 + acb8dfe7f)" and the laptop's latest
section (at bbe37c5f; the counters at 2989c0f0). `test/instrcount.txt` is fast/tttt (BOLT) at a recycle-free K1, so a
jump against it needs attribution (policy, build or code) before it is celebrated.

## Always first: the re-pin to TypeScript 7.1.0 stable
When `npm view typescript dist-tags` shows `latest` 7.1.x (on 2026-10-22: latest 7.0.2, next 7.1.0-dev, beta
7.1.0-beta; 7.1.0 stable expected after 2026-11-24; a beta is not a re-pin target), do this ahead of everything. The
oracle is `main` at fed0bf24 = `typescript@7.1.0-dev.20261008.1`; the dev-to-stable drift was estimated at ~2% of
the suite.
1. `thirdparty/ensure-typescript.sh`: `TS_COMMIT` = the release's `gitHead` (`npm view typescript@7.1.0 gitHead`),
   `TS_VERSION` its version, the Go version from the new `tsc/go.mod` (`GO_VERSION` + sha256). `rm -rf
   thirdparty/typescript` and rerun (~90 s cold; `test/oracle` may need small API fixes if tsgo's internals moved).
2. `thirdparty/refresh.sh`: libs from the new tree; regenerate `src/lib_features.py`, `src/lib_names.py`,
   `src/unicode_identifiers.py`, `src/regex_tables.py` and diff them.
3. `scripts/fasttest.sh --summary`: lost tests are 7.1 changes after the dev pin; bucket them (failstats), fix the
   cheap ones, write up the rest. New tests count too. Then one parallel pass over the new failures (disjoint test
   lists per worker, 0 lost at merge).
4. Every cache regenerated with tsgo (each script's `--tsc`): `realworld.py --tsc --corpus X` for every corpus,
   `sweep.py --tsc` (and `--tsc --flags nuia`, `--flags eopt`), `expecttypes.py --tsc`, `sourcesweep.py --tsc`,
   `flagflip.py --tsc --force`, mutate's /tmp cache deleted. Commit between them (sweep ~10 min, source sweep ~12).
5. `scripts/oracles.py`, `scripts/scenarios.py`, `scripts/clidiff.py --sample 4`: before/after numbers into
   measurements.md; the version in CLAUDE.md's and documentation.md's oracle sentence and vision.md's state.
Done when `grep -rn fed0bf24 scripts src test thirdparty/*.sh` is empty, no oracle in `oracles.py` is STALE, and the
suite's lost list is fixed or written up. tsgo's `.tsbuildinfo` `version` string would matter only if a buildinfo is
ever written (it isn't).

The re-pin above is direction 5 (vision.md "Direction"). The human's priorities for the rest: distinctly faster than
bun; threading efficiency on mid-size and small projects is the key; easy wins first; the correctness long tail
continues.

## For the human
- **The sharing A/B on the laptop** (the prompt: notes/sharing_design.md "The laptop A/B"): plain against today's
  flag, flag + content order, flag + content + the split shard lines, and + batching, on drizzle, zod, effect and vue
  at K12 with `perf stat` and `perf c2c`. The decision numbers are the IPC gap between plain and flag + content +
  split, and vue's wall (2x slower under today's flag). It decides stage 2 (section 4).
- **Keep or revert the relation HACK's removal** (f38a8b493, one commit): react 0.09 -> 0.11 s at the defaults and
  type-fest single 0.61 -> 0.68 on the laptop (bbe37c5f), react +36% / type-fest +7% instructions; it is tsc's own
  relation work (tsgo 84.6k relations on react, tttt 93.4k) and fixed pi-bolt's TypeBox chain. Kept unless the human
  says otherwise.
- **nest's libs-phase sys time under the 4 KB-page AST slabs** (triage.md): the worker measured it down, the merger
  up, both on a loaded box. On the laptop: nest `--time` libs sys and wall, the default against
  `TTTT_AST_SMALL_KB=0`. The RSS win (nest 210 -> 169 MB, react 293 -> 255) stands either way.
- Laptop confirmations of round four: the checker cap (TypeScript6 at K4), the loader not blocking on libs (nest libs
  + load 41 -> 37 ms here), malloc_trim's RSS.

## 1. Small projects
react is bound by one file: `src/components/ui/form/form.tsx` (react-hook-form's Path/PathImpl) is ~44 ms at K1
since the HACK's removal, 55-62 ms at the defaults, the next file 9-29 ms (measurements.md "The 12-core box at
bbe37c5f"). It is tsc's work too: measure tsgo's time on that file (`scripts/tsgoprobe.sh` or a one-file tsconfig)
before optimizing it; react's relation count against tsgo's is in "Smaller ideas". The rest is per-checker
duplication (each checker's arena is its repeated work, 3-6 MB on nest, 12-33 MB on react: notes/front_end.md
"Done"). Gates: RSS at many threads next to the wall; threadcheck/checkerdiff byte-identical.

## 2. Code footprint
Still ~50% frontend-bound after BOLT and huge text (op cache hit 63%, icache misses 23% of fetches at 2989c0f0).
The noinline splits (ty_instantiate_mapped's ~110 copies, hop_whitespace_slow's ~30) simulated I1 misses -4.5..-6%,
but on the laptop vscode K1's icache misses per instruction rose 1.4% and the cycles' -1.7% came from fewer
instructions: the working set is what is left. `scripts/inlinedup.py` and the inlining ledger (notes/code_layout.md)
list the next copies; the parser's hot path, the giant functions' source size, relate.c's inline-unit growth. Every
claim confirmed with the laptop's counters (cachegrind's icache proxy misjudged `-fno-peel-loops`, BOLT and now the
splits).

## 3. Correctness long tail (continuous)
New real-code sightings first; every unfixed divergence gets a known-failing test in `test/cases`. Grep `bugs/` for a
repository's name before investigating it. **No semantics commit lands without a repro or the full source sweep, and
a relation change runs kdiff on the affected project at K1 and K8/16** (better-drizzle's 742 was K>1 only). Hacks
are suspects (vision.md "Decisions"). Open:
- **kebo's mobx-state-tree cycle** (bugs/sourcesweep_checker.md "fourth rule"): tsc resolves a returned function's
  parameter types lazily, tttt eagerly in signature_resolve_slow(); the same root as triage.md's missed TS2502 through
  `ReturnType<typeof h>`. Needs lazy parameter types in signature resolution: a design first, then a repro from MST's
  real types.
- **better-drizzle's two TS2578** at tests/types/order-by-relations.ts:24,26 (uninvestigated; the K>1 TS2502 is fixed).
- **mcp-linker's 1.73x types** (triage.md): the worst perfsweep ratio, a missing cache or a runaway.
- **The indexed-access recursion identity** (triage.md): tsc's rule, reverted for youtube-enhancer's runaway (1.16M
  types against tsgo's 211k); needs a cut-down repro from src/validation/constraints.ts first.
- **The source sweep's other 7** (es-toolkit, monkeytype, hono x2, better-auth, discordx, sunmao): each a confirmed
  tsgo artifact or a written-up design (bugs/sourcesweep_checker.md, bugs/sourcesweep_build.md). **hono's TS2769**:
  tsgo keeps a this-instantiated method type apart from the declared one, tttt merges them (bugs/missing_checks.md;
  known-failing test ttttThisInstantiatedMethodErased).
- **A fifth refresh rule** when the human reports new divergences (the recipe: measurements.md "Fresh repositories,
  third rule" and "fourth rule"); the app-code, low-star and bun slices found what the old oracles couldn't.
- **The missing TS2536** on an indexed access of a recursive conditional alias inside its own declaration (triage.md).
- **The seed-62 spurious getter circularities** (recursiveTypeInference `_zod?` line 28, noCircularitySelfReferentialGetter2
  drop-as): not the expression-memo mechanism; unexplained. Instrumented tsgo (notes/instrumenting_tsgo.md,
  `TSGO_PUSH`/`TSGO_CYCLE`) against `TTTT_DEBUG_CYCLE=1` is the method.
- **The loop-pass K-dependence hole** (triage.md, `ty_flow_partial_reports()`) and the partial pass's leftovers (same).
- **The error pass's first check of a callback argument** (TS2339), **the binding-element family**:
  bugs/missing_checks.md.
- Mutation seeds yield less each time; parser-recovery mutants (58-77 per seed, `scripts/mutate.py`,
  `failstats.py --syntax`) are untouched.

## 4. Mid-size threading: the staged path, as the laptop decides
notes/sharing_design.md "The staged path". The duplication across checkers is inherent per checker (vscode 1.09x at
K12, drizzle 2.15x; notes/duplication.md); affinity, block stealing, cost-estimate dealing, variance sharing and the
persistent pool are closed (vision.md "Decisions").
- **Stage 1, content order by default** (go; a session plus the full, DT and source sweeps): `TTTT_NESTING_ORDER=
  content` becomes the deep-nesting test, push numbers on both relation stacks and inference's as bun has them (the
  prototype's push search is quadratic: +1.6% drizzle). Its parity cost recorded as known-failing: 2 suite tests
  (inferFromNestedSameShapeTuple, ttttDeferredReferenceNormalizedInRelation), drizzle's 3 TS2416 chains, webpack's
  TS2345, valibot's 5 flagflip sites. No gain alone; the precondition for stage 2.
- **Stage 2, the flag on for K > 1 with the plain path at K1** (go only after the laptop A/B: premium ~1.1-1.3x,
  drizzle cycles >= 15% below plain, zod and effect within 5%, vue no longer 2x). Before a default flip: the effect
  cascade (C+V) closed, seeded schedules and 300-run loops, the full oracles flagged and plain identical, the
  `ty_reduced_slow()` KEY_REDUCED_UNION race fixed (triage.md). 2-3 worker-sessions.
- **Stage 3, rollback**: no-go for now.

## Smaller ideas
- **The small-program floor's leftovers** (fe4's leads on nest, laptop, before round three: link 10 ms, walk 7 ms):
  link's time is mostly AST arena madvise/munmap page faulting, not linking (pre-fault or reuse arenas; a parallel
  link per import-cycle level only if link is real work after that); the load pool's finish-file lock is its top
  blocked wait; the walk includes thread start-up on the main thread. The laptop's nest timeline at bbe37c5f: link
  3.0 ms, walk 6.4, 44.7 ms before typecheck.
- **react's relation work against tsgo** (93.4k relations past the cache against tsgo's 84.6k since the HACK's
  removal): small unions related without caching (tsc's skipCaching, prototyped at -12% assignable cache entries) and
  the indexed-access constraint chains tttt relates inside UseFormResetField's variance measurement and tsgo doesn't
  (notes/checker_speed.md, with the trace-alignment method).
- **A snapshot-style recycle for the thrashers** (dokploy, monkeytype, supabase studio): bugs/perf_leads.md "Shared
  warm state at the defaults". Recycling costs vscode 15-20% of check time at small budgets (measurements.md "Memory
  budget sweep on vscode"), so it is the memory direction's lever once speed allows.
- **`-b` shares parsed declaration files across its programs**: every `-b` program parses and binds the default lib
  and each reference output itself (typescript-eslint's 56 programs take at least 80-130 ms each, `TTTT_BUILD_TIMES`);
  tsgo parses each `.d.ts`/`.json` once per build (`parseCache`). Measure the floor's split on a trivial program
  first; then share ASTs and binder scopes read-only between programs where nothing is program-specific
  (`impliedNodeFormat`, language version, arena ownership), or at least the lib files. Gate: typescript-eslint and the
  sweep's biggest solutions (atproto, astro, jest).
- **Source sweep flag flips**: flagflip's cells (`--strict` where the tsconfig isn't, noUncheckedIndexedAccess,
  exactOptionalPropertyTypes) on the source sweep's matching projects, cached per flag like `sweep.py --flags`; the
  missing direction there is unmeasured.
- **Re-entrancy experiment 1 again** (aliases below the call barrier: tsc's rule), now that the "once" flags are
  gone (notes/reentrancy.md).

## Parked (the human decides)
- **CLI modes** (parked 2026-10-16): `--init`, `--showConfig`, `--explainFiles` are each a day or less and
  oracle-able byte for byte; skipping unchanged `.d.ts` writes is small; a `-b` buildinfo (as tttt's own file, never
  tsgo's version string) would let `-b` skip up-to-date projects; watch: W1 (inotify, full rerun) is UX parity, W2
  (affected files by declaration-emit signatures, fresh checkers) is what would beat `tsgo -w` on body edits; full
  `--incremental` and an LSP were judged not worth it. tttt's full check beat tsgo's incremental rebuild on hub edits
  almost everywhere and lost on body edits on 12 of 19 projects (vscode 6.2 vs 5.1 s on the 4-vCPU box). The
  measurements and sizing: `git show af936072c:agents/plans/76_cli_modes.md`.
