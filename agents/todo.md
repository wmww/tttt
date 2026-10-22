# Todo
The live list of directions and ideas, roughly in order (vision.md "Direction" says why). Each entry: what, why, what
is known, where the numbers are. Delete an entry when it lands, leaving its numbers in measurements.md; add new ones
where they fit the order. Uninvestigated items go to triage.md, known bugs to `bugs/*.md`.

Starting point: measurements.md "Oracle totals (2026-10-22, 8a2bb4b38)" and "The 12-core box at f2ad73c9".
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

## 1. The front end's critical path at the defaults
~0.5 s of vscode's 1.93 s on the laptop is serial before checking (libs, the loader thread's own work during
load/parse, declare/link/walk), and it caps every many-core gain; it is also why nest loses to bun (0.08 vs 0.06 s).
Start with a timeline at the defaults on vscode and nest (`--time` with its per-phase main-thread CPU,
`TTTT_LOAD_POOL_STATS=1`, `TTTT_RESOLVE_TIMES=1`, the wall sampler `bench/sampler/`), ideally on the laptop, then cut
the pieces in critical-path order. What is already measured and what was decided against (the import pre-scan,
two-phase link, parallel body parse of one big file) is in notes/front_end.md; those decisions were made at the old
~2.5 s wall and are worth re-checking against the new one. Candidates named there: the config glob (250 M
instructions serial), the loader's diffuse ~80 ms, link's aliases (43 ms), refcounted `ResolvedModule`s.

## 2. Single-threaded checker speed (continuous)
A flat profile; the last round took ~5% (vscode -5.3%, excalidraw -10.9%: measurements.md "Single-threaded checker
speed"). Its leftovers: LiteralBuilder's first realloc, object literal item atoms on the AST, LazyMemo pages. The
stall ranking (notes/checker_speed.md): instruction footprint ~42% (outline cold arms of instantiate_inner and the
relation switch), hash table first probes ~10% (8-byte entries, hot kinds in their own table), Ty header reads ~5%.
Measure by instructions with a same-worktree base build; walls only on the laptop. **The parser** is ~12% of vscode's
K1 instructions and structural (a `Parser` copy per speculative attempt, 74M child creations, no token stream): a
token per position and node arenas, a rewrite of src/parser.c with the suite as its oracle. Do it only after a
measurement shows parse on the critical path at the defaults (it already runs on the pool's threads), or as a spike
on one construct family measured on vscode's parse CPU.

## 3. Cross-checker duplication without the shared graph
At K24 each checker rebuilds the lib's and the project's common types: ~1.2x duplicated work on vscode, and most of
the memory. First a census of what a vscode checker holds and re-derives at the defaults (`scripts/survivors.sh`,
`scripts/arenasites.sh`, per-file sums with `TTTT_FILE_TIMES=1`). Then affinity scheduling (files that share
dependencies dealt to the same checker; today `deal_files()` cuts program order into contiguous runs, tsgo's FENNEL
partitioner duplicated 2-21% more types than that) and the K policy (vscode K24 costs 12 CPU s more than K12 for
0.09 s of wall; measurements.md "The 12-core box at f2ad73c9"). A snapshot-style recycle for the thrashers
(dokploy, monkeytype, supabase studio) is a related idea: bugs/perf_leads.md "Shared warm state at the defaults".

## 4. Hardware counters before the shared graph
The shared graph is parked because each of its instructions costs 1.2-1.6x plain's at K12, inferred (not measured)
to be cross-core cache-line traffic. On the laptop: `perf stat` and `perf c2c record` on drizzle K12, flag
(`TTTT_SHARED_GRAPH=1`) against plain. If the stalls have a layout fix, the residues and the variance-group design
in notes/shared_graph.md "Before reopening it" are next; if not, it stays parked.

## 5. Correctness long tail (continuous)
Each is mechanism-sized; the real-code oracles show them, the suite mostly doesn't:
- **Errors tsc reports from a loop's unfinished flow analysis** (five sightings: vue TS2538/TS4111 in flagflip,
  selfReferencingSpreadInLoop TS2488, typeGuardsAsAssertions TS2339, arrayFrom): tsc reports against an intermediate
  type during loop iteration, tttt only against final types. First learn how tsc's checkExpression inside
  getFlowTypeOfReference's loop pass reports, then whether tttt's compute can report once per node at that point
  (bugs/missing_checks.md).
- **Instantiation identity**: hono's TS2769 (tsgo keeps a this-instantiated method type apart from the declared one;
  tttt merges them) and xstate's eopt TS2322 (tttt makes a second instantiation tsgo reuses), one root.
- **The satisfies elaboration inside the initializer** (TS7022/TS7023), **the error pass's first check of a callback
  argument** (TS2339), **the zod getter case** (re-entrancy stages C/D, notes/reentrancy.md), **the binding-element
  family**: bugs/missing_checks.md.
- Mutation seeds yield less each time (5 fixes at seed 1021, 2 at 1022); parser-recovery mutants (58-77 per seed,
  `scripts/mutate.py`, `failstats.py --syntax`) are untouched.
- bugs/sourcesweep_checker.md and the oracle refreshes (vision.md "Decisions": a new rule for picking repositories,
  day-one rate first).

## Smaller ideas
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
