# Todo
The live list of directions and ideas, roughly in order (vision.md "Direction" says why). Each entry: what, why, what
is known, where the numbers are. Delete an entry when it lands, leaving its numbers in measurements.md; add new ones
where they fit the order. Uninvestigated items go to triage.md, known bugs to `bugs/*.md`.

Starting point: measurements.md "Oracle totals (2026-10-23, 0a400eb6f)" and the laptop's three sections of
2026-10-22 (at 2989c0f0, 8e3f4fb0, 277be4bc). `test/instrcount.txt` is fast/tttt (BOLT) at a recycle-free K1, so a
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
bun; threading efficiency on mid-size and small projects is the key; easy wins first; the correctness long tail
continues.

## For the human
- **The loader not blocking on libs** (notes/front_end.md "Measured and not built"): designed, not built. The
  libs phase stays on the critical path (laptop nest: main blocked ~8 ms on lib.dom before the chunked parse, ~4 ms
  likely now, 4-8 ms of nest's ~80); ~1 day in program_add_source_idempotent() and main.c's libs phase, the riskiest
  ordering code. Build it or not.
- **Re-land the indexed-access recursion identity** (triage.md): tsc's rule, reverted for youtube-enhancer's runaway
  (1.16M types against tsgo's 211k). It needs a cut-down repro from src/validation/constraints.ts first.
- **Laptop confirmations** of round three: the nest timeline (libs with the chunked parse, the directory listings'
  wall), the 16 KB checker policy (32 KB if CPU matters more), huge text's determinism (the `MADV_COLLAPSE` retry),
  the noinline splits' I1 gain (simulated -4.5..-6%), react's new cost (+36% instructions), and whether thread
  creation and re-faulting are serial cost (that would revive the persistent pool).

## 1. Per-checker memory on small projects
react's peak RSS ~300 MB at the defaults against ~169 at `--threads 1` (nest ~195 / ~136), typecheck 16.8k minor
faults against 7.7k, libs 2.9k against 0.2k (`--time` prints faults per phase). Suspects: checker arenas starting on
huge pages (`Arena.huge_first`: 2 MB+ populated per checker for 1-4 MB used), 8 sets of intern tables, per-thread
malloc heaps (triage.md). Gates: RSS at many threads next to the wall; threadcheck/checkerdiff byte-identical.

## 2. Code footprint
Still ~50% frontend-bound after BOLT and huge text (op cache hit 63%, icache misses 23% of fetches at 2989c0f0):
the working set is what is left. Two noinline splits (ty_instantiate_mapped's ~110 copies, hop_whitespace_slow's
~30) took simulated I1 misses -4.5..-6% and the hot text 2.17 -> 2.04 MB; `scripts/inlinedup.py` and the inlining
ledger (notes/code_layout.md) list the next copies. Next: the parser's hot path, the giant functions' source size,
relate.c's inline-unit growth. Every claim confirmed with the laptop's counters (cachegrind's icache proxy misjudged
both `-fno-peel-loops` and BOLT).

## 3. The rest of the small-program floor
After round three (chunked parse, directory listings, resolver shards, clock reads, realpath, wakeups; notes/
front_end.md "Done"), fe4's leads on nest (laptop, before the chunked parse: config 2, libs 16, load+parse 6,
declare 7, link 10, walk 7 ms; typecheck 27):
- **Link's 10 ms is mostly AST arena madvise/munmap page faulting**, not linking: pre-fault or reuse arenas. A
  parallel link per import-cycle level (bun's design) only if link is real work after that; a two-phase link first.
- **The load pool's own mutex** (the finish-file lock) is now the top blocked wait; workers' `file_size_of()` makes
  2.4k stats on nest.
- The walk's 7 ms includes thread start-up on the main thread.
Gates: the suite, corpora, threadcheck/checkerdiff byte-identical; malloc or thread changes an RSS gate at many
threads; walls from the laptop.

## 4. Correctness long tail (continuous)
New real-code sightings first; every unfixed divergence gets a known-failing test in `test/cases`. Grep `bugs/` for a
repository's name before investigating it. **No semantics commit lands without a repro or the full source sweep**
(the sample missed youtube-enhancer's regression). Open:
- **The source sweep's last 7** (es-toolkit, monkeytype, hono x2, better-auth, discordx, sunmao): each a confirmed
  tsgo artifact or a written-up design (bugs/sourcesweep_checker.md, bugs/sourcesweep_build.md). The next refresh by
  a new rule when the human reports new divergences (the recipe: measurements.md "Fresh repositories, third rule").
- **The indexed-access recursion identity's repro** ("For the human" above).
- **The missing TS2536** on an indexed access of a recursive conditional alias inside its own declaration (triage.md).
- **The bun-lockfile slice**: 16 application repositories with only a `bun.lock` were skipped because
  `thirdparty/ensure-sources.sh` has no `bun install` (a pinned bun, `--frozen-lockfile --ignore-scripts`; triage.md
  and repos.txt's "Refresh 4" comment).
- **hono's TS2769**: tsgo keeps a this-instantiated method type apart from the declared one, tttt merges them (design
  in bugs/missing_checks.md; known-failing test ttttThisInstantiatedMethodErased).
- **The seed-62 spurious getter circularities** (recursiveTypeInference `_zod?` line 28, noCircularitySelfReferentialGetter2
  drop-as): not the expression-memo mechanism; unexplained. Instrumented tsgo (notes/instrumenting_tsgo.md,
  `TSGO_PUSH`/`TSGO_CYCLE`) against `TTTT_DEBUG_CYCLE=1` is the method.
- **The loop-pass K-dependence hole** (triage.md, `ty_flow_partial_reports()`) and the partial pass's leftovers (same).
- **The error pass's first check of a callback argument** (TS2339), **the binding-element family**:
  bugs/missing_checks.md.
- Mutation seeds yield less each time; parser-recovery mutants (58-77 per seed, `scripts/mutate.py`,
  `failstats.py --syntax`) are untouched.

## 5. Mid-size threading: a design memo, not a build
The duplication across checkers is inherent per checker (vscode 1.09x at K12, drizzle 2.15x; notes/duplication.md);
affinity, block stealing, cost-estimate dealing, the shared graph and variance sharing are measured and closed, the
persistent pool declined (vision.md "Decisions"). The one shape not yet tried is bun's: types published at creation
into one hash-consed store, results in per-file batches into dense tables, ~90% never published
(notes/bun_concurrency.md). A memo must first show how tttt's output stays deterministic without bun's rollback
(which needs id-free ordering). Only then a prototype, measured on drizzle (the laptop: the shared-graph flag's -26%
instructions bought only -9% cycles there).

## Smaller ideas
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
