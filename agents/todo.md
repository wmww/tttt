# Todo
The live list of directions and ideas, roughly in order (vision.md "Direction" says why). Each entry: what, why, what
is known, where the numbers are. Delete an entry when it lands, leaving its numbers in measurements.md; add new ones
where they fit the order. Uninvestigated items go to triage.md, known bugs to `bugs/*.md`.

Starting point: measurements.md "Oracle totals (2026-10-22, 2989c0f05)" and "The 12-core box at f2ad73c9".
`test/instrcount.txt` is at a recycle-free K1 (the memory default), so a jump against it needs attribution (policy or
code) before it is celebrated.

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

The re-pin above is direction 5 (vision.md "Direction").

## 1. The parser as a structural project
~12% of vscode's K1 instructions with no hot site (a `Parser` copy per speculative attempt, 74M child creations for
138 MB of source, no token stream), and on the check phase's critical path at the defaults: deferred bodies are parsed
on the checker threads. First measure parse CPU inside the check phase with `bench/timeline/` (the whole-process
timeline tool) at the defaults on vscode, so the win is priced before the rewrite. Then a token per position and node
arenas, a rewrite of src/parser.c with the suite and `scripts/syntaxstats.py` as its oracle (error recovery stays: the
one list/commit-point framework, vision.md "Decisions"). A spike on one construct family measured on vscode's parse CPU
is a fine first step if its code becomes the skeleton.

## 2. The memory system
IPC is ~1.2 and cachegrind estimates half the cycles stalled; checker constant factors are mined out (0.1-0.5% a site,
notes/checker_speed.md). The stall ranking there (instruction footprint ~42%, hash table first probes ~10%, Ty header
reads ~5%) is a simulation: before choosing between hash-table layout (8-byte entries, hot kinds in their own table),
`Ty` header layout and allocation volume, get the laptop's hardware counters: `perf stat` and `perf record` at K1 and
at the defaults, topdown, on vscode and drizzle. Lazy message formatting (2.6% on drizzle) is blocked on hacks.md's
"Two circularity diagnostics come from printing a type for its side effects" being fixed tsc's way first.

## 3. Front end round 2 (after the laptop's timeline)
Round 1 landed (the include walk on its own thread streaming roots to the loader, prefetch in link's alias loop, module
refs on threads, a 16 MB malloc top pad during the front end, `absolute_path_fast`). Next, once the laptop's
`bench/timeline/` run shows what is left serial: names interned per program (link, declare and the walk compare names
in source text today), then the two-phase link (notes/front_end.md; decided against at the old ~2.5 s wall, worth
re-checking). Any malloc tuning (`M_TOP_PAD` and friends) needs an RSS gate at many threads.

## 4. Correctness long tail (continuous)
New real-code sightings first; every unfixed divergence gets a known-failing test in `test/cases`. Grep `bugs/` for a
repository's name before investigating it (the source sweep's leftovers are all written up there). Open:
- **hono's TS2769**: tsgo keeps a this-instantiated method type apart from the declared one, tttt merges them (design
  in bugs/sourcesweep_build.md; known-failing test ttttThisInstantiatedMethodErased).
- **The seed-62 spurious getter circularities** (recursiveTypeInference `_zod?` line 28, noCircularitySelfReferentialGetter2
  drop-as): not the expression-memo mechanism; unexplained. Instrumented tsgo (notes/instrumenting_tsgo.md,
  `TSGO_PUSH`/`TSGO_CYCLE`) against `TTTT_DEBUG_CYCLE=1` is the method.
- **The loop-pass K-dependence hole** (triage.md, `ty_flow_partial_reports()`) and the partial pass's leftovers (same).
- **The error pass's first check of a callback argument** (TS2339), **the binding-element family**:
  bugs/missing_checks.md.
- Mutation seeds yield less each time; parser-recovery mutants (58-77 per seed, `scripts/mutate.py`,
  `failstats.py --syntax`) are untouched. Oracle refreshes with repositories picked by a new rule (vision.md
  "Decisions").

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
