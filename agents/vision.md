# Vision

## Goal
A complete TypeScript typechecker in C, **distinctly faster than bun canary's `bun check`** (the fastest competitor:
measurements.md "The 12-core box at 5f2cd699"), with typescript-go 7.1 (tsgo) as the oracle for every diagnostic,
type and emitted declaration. Restated by the human on 2026-10-22: speed first; memory is secondary until we are
faster; correctness is never traded. Completeness first in the sense that nothing is built on a foundation that can't
be fast or can't be complete. The suite's baselines are the first oracle, not the goal; real projects that are clean
under tsc are the second.

## State (2026-10-22: oracles at 8a2bb4b38, the laptop bench at f2ad73c9)
- **Target: TypeScript 7.1** (`main` at the 7.1 nightly fed0bf24, `thirdparty/typescript`; re-pin to 7.1.0 stable
  when it is on npm: todo.md). Suite with own tests and the dts component **13972 / 14006** (dts 1888 / 1910); the
  native suite's last 12 configurations are argued or one-test mechanisms (bugs/tsgo_divergences.md).
- **Drop-in `tsc --noEmit`, and drop-in `tsc` where the config only emits declarations**, TypeScript and JavaScript
  alike. clidiff (the whole `--pretty false` output on every corpus and source sweep project) **3824 / 3831**, dtsdiff
  **1328 / 1350** projects. Refused: `--watch`, `--incremental`'s `.tsbuildinfo`, `--showConfig`, `--explainFiles`,
  `--init` (parked by the human). JS emit is out of scope.
- **Real code**: corpora 0 / 0 everywhere but vue (0 / 2, a tsgo runaway artifact), DT sweep 8595 / 8595,
  `$ExpectType` 68640 / 68640, flagflip 18 / 3 / 2, scenarios 221 / 0 failed, source sweep 5235 / 5242 projects
  (2026-10-20; quarter sample 1272 / 1274), `-b` sweep 411 / 416 solutions (the 5 are tsgo's), kdiff 3, recycle 3
  (rxjs, sequelize x2: tsgo-shared).
- **Speed** (the human's 12-core laptop, 24 threads; measurements.md "The 12-core box at f2ad73c9" and "The
  12-core box at 5f2cd699"): tttt wins or ties bun canary on 11 of 14 corpora at the defaults. **vscode is the
  gap**: tttt 1.93 s / 35 CPU s / 5.4 GB at the default K24, bun 1.74 / 36 / 2.2 GB. Single-threaded tttt is fastest
  everywhere (vscode K1 9.9 s; bun single 15.6 and tsc-rs 16.1 at 5f2cd699). At K24 tttt and bun spend the same CPU: the wall gap is the serial front end (~0.5 s of
  vscode's 1.93: libs, the loader thread's own work, declare/link/walk; notes/front_end.md). The check phase's parallel
  efficiency is already ~80% once all-core clocks (1.62x slower than single-core boost, measured) and SMT are
  accounted for; what is left there is ~1.2x duplicated work across checkers (each rebuilds the lib and the project's
  common types; it is also the memory).
- **Memory**: the quarter-of-RAM `--checker-memory` default costs vscode 5.4 GB (K1 4.1 GB, was 1.4). A sweep on
  2026-10-22 (16 threads) showed recycling costs 15-20% of check time on vscode and ~2 GB of the peak is the program
  plus per-thread state whatever the budget: no knob fixes it; structural, deferred until we are faster.
- Instructions: `test/instrcount.txt` (saved at 9f979e91c, a recycle-free K1).

## Direction (2026-10-22; the items and their numbers are in todo.md)
1. **The front end's critical path at the defaults.** Amdahl: the serial ~0.5 s caps every many-core gain and is
   why nest loses 0.08 vs 0.06 s. Timeline first (`--time`, the wall sampler in `bench/sampler/`), then the pieces.
2. **Single-threaded checker speed, continuously.** A flat profile; ~5% per focused round from frames, allocation and
   memos. The parser's ~12% of instructions belongs here (a token stream and node arenas), but only after a
   measurement shows parse on the critical path at the defaults.
3. **Cross-checker duplication without the shared graph**: a census of what a vscode checker holds and re-derives,
   then affinity scheduling (files sharing dependencies to the same checker) and the K policy (K24 costs 12 CPU s
   for 0.09 s on vscode).
4. **Hardware counters on the laptop** (`perf stat`, `perf c2c`, drizzle K12 flag vs plain) before any return to the
   shared graph. bun's 2.2 GB at full parallelism is the existence proof that sharing can pay.
5. **The correctness long tail, continuously**: loop-flow analysis errors, instantiation identity, the satisfies
   elaboration, the error pass's first callback check, the zod getter, binding elements.

## Decisions (each with its reason; the narrative is in git history of this file)
- **The shared type graph is parked** (2026-10-21/22): built correct by construction and in the tree behind
  `TTTT_SHARED_GRAPH=1`, but at K12 each of its instructions costs 1.2-1.6x plain's, which outweighs the duplicated
  work it saves on ordinary projects, and its output still differs under load on effect (~1%). It wins 2.2-2.5x only
  on recycle thrashers (dokploy, monkeytype). Reopen only with hardware counters and a design for variance groups
  cut by deep nesting (notes/shared_graph.md). The earlier lock/wait/redo design was deleted, not narrowed: waiting
  creates the cycles a redo must break, and the redo's history is no sequential checker's.
- **No shared declared layer** (2026-10-18): what checkers re-derive is instantiations of the project's own code; lib
  and `.d.ts` declarations are 2-6% of it (measurements.md "What the checkers re-derive").
- **Defaults** (2026-10-22, kept by the laptop bench): `--threads` every core; checkers one per 64 KB of checked
  source up to the physical cores, past them one per 4 MB up to the threads (only vscode gets 24); `--checker-memory`
  a quarter of RAM (256 MB..8 GB; zero recycles on everything measured); a stolen file starts a fresh checker only
  when the thief holds half its budget. A wall gain must be weighed against CPU: K12 -> K24 on vscode bought 0.09 s
  for 12 CPU s.
- **Memory is secondary until we are faster** (the human, 2026-10-22; up to ~3x bun's peak is fine for a clear speed
  win). Closed on the way: the compact AST (2026-10-16: vscode's zero words are diffuse, exact-size variants reach
  ~165 MB at most), the copying recycle (2026-10-17: costs about what re-derivation does), further struct diets
  (< 120 MB left), freeing ASTs after their file (other files' checks read them).
- **Output is independent of checker count, threads, budget and schedule**, byte for byte (`scripts/kdiff.py`,
  `checkerdiff.py`, `threadcheck.sh`, the recycle oracle): tsgo's is. A change that makes a flake rarer without a
  named mechanism is not a fix. Anything that could be history must be derived from the node or declaration, as tsc
  does, not from what a checker happened to compute first.
- **Correctness by construction over catching races**, and over fidelity to tsgo *artifacts* (a runaway's model, a
  cache collision): emulating those is special-casing in a respectable form. The suite's last 12 are off the table
  (2026-10-15): each is argued in bugs/tsgo_divergences.md or a mechanism needed by one test; work them only if real
  code shows the mechanism. A test may be declared out of reach only with evidence that tsgo's output depends on
  something tttt shouldn't reproduce ("tsgo's check order" alone is not such evidence).
- **The oracle is tsc as it ships** (tsgo 7.1): no dual-version modes, delete what the oracle deleted, re-pin when a
  release lands. The oracle tools are tsgo built from source with a Go overlay (`test/oracle`), so instrumenting tsgo
  stays one `go build` away.
- **Real projects are the second oracle, and oracles must look both ways.** A rising pass count with flat corpus
  counts is test-chasing; a clean corpus only finds spurious diagnostics, flag flips and mutations find missing ones.
  Rank by root cause across projects, not by code (one mapped type with an `as` clause was 810 TS7006); prefer fixes
  that move whole categories. Saturated oracles measure nothing: refresh the breadth oracles with repositories chosen
  by a new rule every so often (day-one match rates 96.1, 97.1, 98.1%; the recipe is measurements.md "Fresh
  repositories"), and say what each oracle's projection doesn't compare (message text was a hidden axis once).
- **Perf is priced by instructions and types, not wall, on the shared box**: callgrind counts (`scripts/instrcount.sh`
  at every landing: creep is silent otherwise, a +50% on drizzle once went unnoticed), tsgo's type and instantiation
  counts as a machine-independent oracle (`scripts/perfsweep.py`: a ratio above ~1.5 is a missing cache or a
  runaway), many-core work by per-file time sums under real parallel runs with the spinning-cores floor (1.62x), never
  serialized CPU. Walls come from the human's laptop. PGO + LTO ships (`scripts/pgobuild.sh`, -11..-17%).
- **The parser's error recovery needs no rewrite** (2026-09-29: one list/commit-point framework). A rewrite for speed
  (tokens, arenas) is a separate question, decided by a profile at the defaults.
- **JS lowers into the same `Ty` machinery**: no parallel JSDoc type world.
- **Default lib files are checked like tsgo checks them** (unless skipLibCheck/skipDefaultLibCheck): ~40 ms per
  esnext+dom program, and it found a real checker bug.
- **`-b` has one model** (2026-10-20): a referenced project is read through its emitted declarations, like `tsc -b`;
  its leftovers are `-p` checker bugs (bugs/sourcesweep_build.md).
- **Out of scope**: JS emit (2026-10-12), a cache across runs (2026-10-21), the CLI modes above (parked 2026-10-16;
  the measurements for when they reopen are in todo.md "Parked").
- **How changes land**: staging in place works (the type core migration ran 8 steps with the suite runnable
  throughout); don't transliterate weak algorithms onto a new foundation, write them the way tsc observably behaves;
  when relations get stricter, expect latent inference bugs a lenient relation hid; when a checker feature "doesn't
  work" on lib types, look at what the lib *parsed* to first. A drop-in must survive its inputs changing mid-run
  (`text_sigbus()`).

## Built and holding
Hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks), tsc-shaped binder with persistent
symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity relation, stable type ordering,
mapper instantiation, tsc's inference flow (return mappers included) and ContextChecked/context anchors, one
circularity stack with resolution barriers, conditional/mapped/indexed/template/substitution types, deferred
references, flow graph + narrowing, module resolution + tsconfig with tsgo's two-phase file collection, iteration
protocol, generators, `this` types, mixins, decorators, union origins, elaboration, JSX, tsc's type printer and symbol
chains, the pseudochecker, JSDoc + CommonJS + expandos as 7 has them, parallel loading / declaring / resolving /
checking with the output independent of the checker count, work stealing, checker recycling, deferred function
bodies, per-file AST arenas, mapped source text, declaration diagnostics, the declaration writer with comments and
maps for TypeScript and JavaScript, project references / `-b` through emitted declarations, 7.1's content mappers,
tsc's laziness and deferred checks, 7.1's language, in-flight state as per-checker records, one atom table and one
type-id space per program, the shared graph behind its flag. ~150k lines of C.

## Absent or approximated
Pseudochecker gaps (bugs/display.md "Pseudochecker gaps"), namespace-import clones (cloneTypeAsModuleType), member
identity (members aren't Symbols), tsc's re-entrancy beyond the declaration and return-type sites
(notes/reentrancy.md), errors reported from a loop's intermediate flow types, the CLI modes above.

## Architecture assessment
A clean, direct-style checker: hand-written backtracking parser straight from `char*` (no tokenizer, a fail cache for
the ambiguous cases), an AST of plain structs in per-file arenas, a binder (persistent symbols and scopes, merging
like tsc's), then checking driven by `LazyType` (memoised, cycle-detecting per-node types) over the interned `Ty`
core. All checker state is in `Checker`; the AST is read-only while checking. What's left to watch:
- **Parser**: unbounded recursion depth (binary chains need ~8 MB of stack; tsc walks them iteratively), the
  structural cost above (a `Parser` copy per attempt, no token stream).
- **Binder**: declare runs in parallel for files that declare only into their own tables (96% of vscode's files);
  link is serial by design (alias resolution's order is observable; a two-phase design exists, not worth it now:
  notes/front_end.md); the resolve walk shards per file (hacks.md "Parallelism contract").
- **Threading**: any new checker-wide memo that *reports* must be tagged `Error.once_key`, and anything written to
  AST/binder state during checking is a race (the frozen guards log it; TSan on the corpora is the net:
  documentation.md "Typecheck"). Under `--shared-graph` a new lazily written field must be a `SharedWord`
  (notes/shared_graph.md) or it is a race only TSan and the 20-run tables find.

## Risks
- Exact-match oracle: `types`/`symbols` baselines require tsc's type printing (alias preservation, union order,
  origins, truncation). Type ids still break union order's last ties (fresh/regular literals, same-symbol anonymous
  types, conditional roots), so creation order still matters a little.
- Perf: checks accumulate cost silently between perf rounds; deep generic code is where tsc's caches and shortcuts
  matter (every past check-up's wins were tsc caches tttt lacked).
- **Competition**: bun canary wins vscode's default wall and holds a third of tttt's memory there; tsc-rs (a Rust tsgo
  port at our pin, `scripts/bench.sh --tscrs`) spends the least CPU at few checkers. Our margin is single-threaded
  efficiency, which many cores spend on all-core clocks, SMT and duplicated work.
- tsgo is order dependent in places tttt isn't; tsgo 7.1's ordering rules are what `ty/order.c` must port, not patch.
