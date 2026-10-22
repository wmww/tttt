# Todo
The live list of directions and ideas, roughly in order (vision.md "Direction" says why). Each entry: what, why, what
is known, where the numbers are. Delete an entry when it lands, leaving its numbers in measurements.md; add new ones
where they fit the order. Uninvestigated items go to triage.md, known bugs to `bugs/*.md`.

Starting point: measurements.md "Oracle totals (2026-10-23, d01aee7a8)" and the laptop's three sections of
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

## 1. The small-program floor (front end, easy wins first)
fe3's leads (2026-10-23; numbers from nest and vscode on this box, strace and the timeline):
- **lib.dom's parse**: 2.4 MB on one thread, 12-20 ms of nest's libs phase; the chunked parse is sketched in
  notes/front_end.md "The parser" (the walk is already chunked: `TTTT_WALK_CHUNK_BYTES`).
- **A persistent thread pool**: nest starts 71 threads (react 67), each phase creates and joins its own (~1 ms on
  nest). ~58 thread-locals assume one phase per thread: audit them first.
- **Link in parallel per import-cycle level** (bun's design, notes/bun_concurrency.md): 30-40 ms on vscode.
- **Stats from directory listings** in CachedFs: nest 7.8k `newfstatat` + 10k `readlink` (8.4k failing), vscode
  8.2k + 10.7k.
- **The module resolver's lock**: `module_resolver_resolve_shared`'s lock is ~57% of vscode's blocked load-phase
  samples. nest's remaining futexes are 85% the load pool's own mutex and condvars.
- **`clock_gettime` as real syscalls** (nest 4.4k, vscode 21k): some clock isn't vDSO-served.
- The `read()` hybrid for files under 16 KB (`TTTT_TEXT_READ_BELOW`) covers 97% of nest's files, 44% of its bytes;
  its chunks must stay `MAP_POPULATE`.
Gates: the suite, corpora, threadcheck/checkerdiff byte-identical; malloc or thread changes an RSS gate at many
threads; walls from the laptop.

## 2. Checker threads' malloc heaps
Checker threads grow their glibc heaps 4 KB at a time: ~13k `mprotect`s on vscode, 3-7% of checker samples (mm lock
held for writing while others fault). Sites: lazy_type_state 7141, declare_class_members 1816, declare_symbol_flags
1435, literal_builder_add 911, unused_register 619 (triage.md). Move them into the checker's arena; `M_TOP_PAD` does
not help (tried). unused_register's cross-arena free is already fixed (malloc-lock waits 8% -> 0.6% of samples).

## 3. Code footprint
Still ~50% frontend-bound after BOLT and huge text (op cache hit 63%, icache misses 23% of fetches at 2989c0f0):
the working set is what is left. Candidates in notes/code_layout.md: fewer inlined copies, relate.c's inline-unit
growth, the parser's hot path. Every claim confirmed with the laptop's counters (cachegrind's icache proxy misjudged
both `-fno-peel-loops` and BOLT).

## 4. Correctness long tail (continuous)
New real-code sightings first; every unfixed divergence gets a known-failing test in `test/cases`. Grep `bugs/` for a
repository's name before investigating it (the source sweep's leftovers are all written up there). Open:
- **The third refresh (cor3)**: 40 repositories, day one 115 / 122 projects (94.3%), 4 root causes: TypeBox's
  Record/Enum partial, a Vue auto-import global, a spurious TS7022, a TS2322 on select options.
- **The bun-lockfile slice** next: 16 application repositories with only a `bun.lock` were skipped because
  `thirdparty/ensure-sources.sh` has no `bun install` (a pinned bun, `--frozen-lockfile --ignore-scripts`; wt-cor3's
  triage entry and repos.txt's "Refresh 4" comment).
- **hono's TS2769**: tsgo keeps a this-instantiated method type apart from the declared one, tttt merges them (design
  in bugs/sourcesweep_build.md; known-failing test ttttThisInstantiatedMethodErased).
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
affinity, block stealing, cost-estimate dealing, the shared graph and variance sharing are measured and closed
(vision.md "Decisions"). The one shape not yet tried is bun's: types published at creation into one hash-consed
store, results in per-file batches into dense tables, ~90% never published (notes/bun_concurrency.md). A memo must
first show how tttt's output stays deterministic without bun's rollback (which needs id-free ordering). Only then a
prototype, measured on drizzle (the laptop: the shared-graph flag's -26% instructions bought only -9% cycles there).

## Smaller ideas
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
