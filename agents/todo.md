# Todo
The live list of directions and ideas, roughly in order (vision.md "Direction" says why). Each entry: what, why, what
is known, where the numbers are. Delete an entry when it lands, leaving its numbers in measurements.md; add new ones
where they fit the order. Uninvestigated items go to triage.md, known bugs to `bugs/*.md`.

Starting point: measurements.md "Oracle totals (2026-10-23, d7bc837b0)" and the laptop's latest section (at
3a77c827: the kernel round; the counters at 2989c0f0). `test/instrcount.txt` is fast/tttt (BOLT) at a recycle-free K1, so a
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

The re-pin above is direction 6 (vision.md "Direction"). The human's priorities for the rest: distinctly faster than
bun canary on real projects, judged on their private set (perf work needs a mechanism argument or a breadth check,
not the bench: vision.md "Decisions"); speed over memory, but memory is worth cutting where it is cheap; easy,
measured wins first; correctness never traded.

## For the human
The prompts were given in conversation at the fourth coordinator's end; redo them from here, each with the exact
command (measurements.md "How to measure": sys with `TTTT_SYNC_EXIT=1`):
- **(a) The remaining kernel time**: `perf record -e cycles:k -g` top symbols on drizzle and vscode at the defaults
  (kernel cycles are 6-9% of user there, 17% on react, 27% on nest). Feeds section 4.
- **(b) The exit tail under THP `always`**: last output byte to exit with `TTTT_SYNC_EXIT=1` against the reaper's
  default (this box, THP `madvise`, measured 7-66 ms; fewer pages to free on the laptop may make it smaller).
- **(c) Thread placement on the hybrid CPU**: `taskset` tttt to the 8 Zen 5 threads, all 24, the 16 Zen 5c, and 12
  one per core, on drizzle and vscode, wall and CPU. Feeds sections 2 and 4 (checker count policy).
- **(d) The recycle-budget sweep**: `--checker-memory` 4096 / 3072 / 2048 against the default on vscode and drizzle,
  wall against peak RSS. Decides section 3's first lever.
- **(e) The bench on main after the reaper** (`scripts/bench.sh --runs 3 --checkers 12 --bun ...`), the arena
  alignment (97839044a) included.
- **Keep or revert the relation HACK's removal** (f38a8b493, one commit): react 0.09 -> 0.11 s at the defaults and
  type-fest single 0.61 -> 0.68 on the laptop (bbe37c5f), react +36% / type-fest +7% instructions; it is tsc's own
  relation work (tsgo 84.6k relations on react, tttt 93.4k, and form.tsx is half of tsgo's react check too) and fixed
  pi-bolt's TypeBox chain. Kept unless the human says otherwise.
- MADV_POPULATE_WRITE counts as minor faults on this box's kernel but not in the laptop's `perf stat page-faults`
  (6.7k there against ~72k here on drizzle): compare faults like with like.

## 1. Page-fault leftovers (small)
The arena alignment landed (97839044a: huge arena chunks are whole aligned 2 MB pages; faults drizzle 72k -> 44k,
vscode 301k -> 185k, sys -12..-20%, RSS +0..2%; measurements.md "2 MB-aligned huge arena chunks"); the laptop's (e)
should include it. Not started from ktime2's brief: does the pre-check malloc_trim still pay (RSS and madvise count
with `TTTT_NO_TRIM=1`), and pre-faulting checkers' first chunks off the critical path (resolve names keeps ~4.5 of 16
cores busy: a helper could populate them then).

## 2. The bun breadth sweep and its critical-path fixes
Where the human's remaining losses to bun live. Not started: notes/bun_breadth.md has the tool (`scripts/perfshape.sh
-p P --bun PATH`, f903d2e0e), how to get bun canary 1.4.4-canary.20261022.1, the sample (the ~500 largest of
perfsweep's 767 projects with tsgo `check_s >= 0.3`) and the loss classes. The human's one fact about their losing
projects: tttt uses less total CPU on every one, so the losses are critical path (idle cores), not duplication. One
sample: trpc/packages/tests keeps 4 of 16 cores busy with 8 checkers, its costliest file 41% of the checkers' span.
Expected remedies: policy and scheduling (more checkers when the critical path is long, splitting what one dominating
file holds, starting checking earlier). Warm bun once per project (its first run was seconds slower).

## 3. Memory: three levers, cheapest first
vscode 5.5 GB at the defaults against bun's 2.2 (K1 4.3); the kernel round added +4..15% (vision.md "Decisions").
- **The recycle-budget default** from the laptop's sweep (For the human, d): a fixed cap rather than a quarter of RAM
  if 2-3 GB costs little wall.
- **Per-file release**: drop what a checker made for one file at that file's end, as bun frees its per-task results
  (most of its 2.2 GB on vscode). wt-memc's census (a6af311ed: `scripts/filecensus.sh`, a `-DTTTT_SURVIVORS` build,
  `TTTT_FILE_CENSUS=1`; method, partial table and design sketch in agents/notes/memory.md):
  **zod at K1, 82% of a checker's 275 MB arena is droppable at file end** (11% dead, 71% still referenced by later
  files' results but not needed), mostly instantiated type structure (instantiate_members ~73 MB, builder_finish ~18,
  mapped expansion ~13), not expression memos (4.5 MB); dropping it re-derives ~4% of types. The catch: a memo- and
  scratch-only region is safe but reaches only 5-35 MB on zod; the full 80% needs "everything per file, survivors
  copied out at file end", which is the copying recycle declined on 2026-10-17 (measurements.md "A copying checker
  recycle"): reprice it against this census before deciding. Next: drizzle, effect, vscode at K1 and the defaults.
  Gates: kdiff and the recycle oracle (a dropped result must never be read again), instructions.
- **Phase-boundary trims**: a trim burst when no checker runs. Small.

## 4. Kernel round three
From the laptop's `cycles:k` profile (For the human, a) and the thread-placement experiment (c). What was measured and
declined in round one is in notes/kernel_time.md ("Measured and not changed": glibc.malloc.hugetlb=1's faults -23..-35%
at +19% RSS on nest). The rest of malloc (AST helpers, scopes, flow) is the next fault lever.

## 5. Correctness long tail (continuous)
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

## 6. Small projects
react is bound by one file: `src/components/ui/form/form.tsx` (react-hook-form's Path/PathImpl with zodResolver) is
~37-44 ms of a ~94 ms K1 typecheck. **tsc pays the same** (measurements.md "The 12-core box at 48cd9835" part C: about
half of tsgo's check time and 74% of its types; tttt does it 5x faster), so it bounds react at every checker count;
react's relation count against tsgo's is in "Smaller ideas". nest is start-up bound (the front end is 2/3 of its
55 ms; kernel 27.5% of user cycles after the kernel round, against 6-9% on the big corpora). The rest is per-checker
duplication (each checker's arena is its repeated work, 3-6 MB on nest, 12-33 MB on react: notes/front_end.md
"Done"). Gates: RSS at many threads next to the wall; threadcheck/checkerdiff byte-identical.

## Smaller ideas
- **Code footprint**: still ~50% frontend-bound after BOLT and huge text (op cache hit 63%, icache misses 23% of
  fetches at 2989c0f0). The noinline splits simulated I1 misses -4.5..-6%, but on the laptop vscode K1's icache misses
  per instruction rose 1.4% and the cycles' -1.7% came from fewer instructions: the working set is what is left.
  `scripts/inlinedup.py` and the inlining ledger (notes/code_layout.md) list the next copies. Lower priority since
  BOLT and the huge text page didn't move the human's private set: a change here needs a mechanism argument and
  the laptop's counters, not the bench.
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
