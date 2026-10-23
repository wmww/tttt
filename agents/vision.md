# Vision

## Goal
A complete TypeScript typechecker in C, **distinctly faster than bun canary's `bun check`** (the fastest competitor:
measurements.md "The 12-core box at 5f2cd699"), with typescript-go 7.1 (tsgo) as the oracle for every diagnostic,
type and emitted declaration. Restated by the human on 2026-10-22: speed first; memory is secondary until we are
faster; correctness is never traded. Completeness first in the sense that nothing is built on a foundation that can't
be fast or can't be complete. The suite's baselines are the first oracle, not the goal; real projects that are clean
under tsc are the second.

## State (2026-10-23: oracles at a5252a764 plus cor7's acb8dfe7f, main 0a1f67836; the laptop at bbe37c5f)
- **Target: TypeScript 7.1** (`main` at the 7.1 nightly fed0bf24, `thirdparty/typescript`; re-pin to 7.1.0 stable
  when it is on npm: todo.md). Suite with own tests and the dts component **13999 / 14035** (dts 1888 / 1910), the
  same with `--no-defer-bodies`, `--checker-memory 0` 0 of 17671 differ; two of tttt's own tests fail by design
  (known-failing tests for unfixed divergences: ttttThisInstantiatedMethodErased, hono's this-instantiated members;
  ttttDeferredBodyBeforeContextualParameters, tsc's deferred-body check order); the native suite's last 12
  configurations are argued or one-test mechanisms (bugs/tsgo_divergences.md).
- **Drop-in `tsc --noEmit`, and drop-in `tsc` where the config only emits declarations**, TypeScript and JavaScript
  alike. clidiff (the whole `--pretty false` output on every corpus and source sweep project) **3974 / 3986**, dtsdiff
  **1328 / 1350** projects. Refused: `--watch`, `--incremental`'s `.tsbuildinfo`, `--showConfig`, `--explainFiles`,
  `--init` (parked by the human). JS emit is out of scope.
- **Real code**: corpora 0 / 0 everywhere but vue (0 / 2, a tsgo runaway artifact), DT sweep 8595 / 8595,
  `$ExpectType` 68640 / 68640, flagflip 16 / 2 / 2 (all 20 vue pluginScoped.ts, tsgo's TS5115 runaway: won't fix),
  scenarios 221 / 0 failed, source sweep (316 repositories, 5464 projects since refresh 5's 28 bun-lockfile repos)
  quarter sample 1322 / 1324; the full sweep 5454 / 5464 before cor7, whose fix removed better-drizzle's
  checker-count-dependent TS2502 since. Left: es-toolkit, monkeytype, hono x2, better-auth, discordx and sunmao (each a
  confirmed tsgo artifact or a written-up design), kebo's mobx-state-tree cycle (eager signature parameters) and
  better-drizzle's two TS2578 (bugs/sourcesweep_checker.md, bugs/sourcesweep_build.md); `-b` sweep 411 / 416
  solutions (the 5 are tsgo's), kdiff 3 / 3969, recycle 3 / 3969 (rxjs, sequelize x2: tsgo-shared; dokploy's recycle
  run times out under load: triage.md), perfsweep median check ratio 0.28 (measured loaded; 0.17 last round), worst
  types ratio **1.73 (mcp-linker, refresh 5, unexamined: triage.md)**. Refresh 5 matched 94 / 100 projects on day one
  (the same 94% as refresh 4), 97 / 100 after five fixes.
- **Speed** (the human's 12-core laptop, 24 threads; measurements.md "The 12-core box at bbe37c5f"): **tttt is ahead
  of bun canary's wall on every corpus** at the defaults; vscode 1.70 s alone (level with 277be4bc; bun's 1.74 there).
  nest 84 -> 58.6 ms median (-30%: the chunked parse of lib.dom took libs 12-19 -> 7 ms, 0 fallbacks); zod, vue and
  TS6 -b faster. **react 0.09 -> 0.11 s and type-fest single 0.61 -> 0.68** are the relation HACK's removal: react's
  form.tsx is 8 -> 44 ms at K1 alone, tsc's own work there, and bounds react at every checker count. Single-threaded
  tttt is fastest everywhere. Hardware counters: tttt is **frontend-bound** (~50% of slots at K1), IPC 1.42 at K1; the
  noinline splits took vscode K1 cycles -1.7% through fewer instructions, the simulated icache gain didn't show.
  Round four's front end (the checker cap, AST slabs' first 4 MB on 4 KB pages, malloc_trim, the loader not blocking
  on libs) is unmeasured on the laptop (todo.md "For the human"). What is left at the defaults: the small-program
  floor (react's one file, per-checker memory: todo.md 1) and duplicated work across checkers (vscode 1.09x at K12,
  mostly inherent: notes/duplication.md).
- **The shipped build is `scripts/fastbuild.sh` -> `fast/tttt`**: GCC LTO + PGO, `-fno-plt`, `-z now`, then an
  llvm-bolt layout (llvm-bolt is a dependency), the hot text on a huge page at startup (`huge_text()`, verified via
  smaps_rollup, with an `MADV_COLLAPSE` retry; `TTTT_HUGE_TEXT=0/2`: 20 of 20 nest runs took it on the laptop). The
  measuring scripts take their binary from `fastbuild.sh --measured`. `release/` (plain -O3) stays the edit-test build.
- **Memory**: the quarter-of-RAM `--checker-memory` default costs vscode 5.4 GB (K1 4.1 GB). Recycling costs 15-20%
  of check time on vscode at small budgets and ~2 GB of the peak is the program plus per-thread state whatever the
  budget: structural, deferred until we are distinctly faster. Small programs: AST slabs' first 4 MB per thread on 4
  KB pages and a malloc_trim before checking took nest's peak 210 -> 169 MB and react's 293 -> 255 at the defaults;
  the checkers' arenas are each checker's repeated work, not start-up slack (notes/front_end.md "Done").
- Instructions: `test/instrcount.txt` (fast/tttt, every corpus, at acb8dfe7f): **vscode 66.39 G**. Round four was flat
  (0a400eb6f -> acb8dfe7f: vscode +0.06%, type-fest -0.5%, vue -0.4%, the rest within 0.2%); react's +36% and
  type-fest's +7% of round three stand (tsc's relation work: Decisions).

## Direction (2026-10-23, after round four at 0a1f67836; the items and their numbers are in todo.md)
The human's priorities: distinctly faster than bun; threading efficiency on mid-size and small projects is the key;
easy wins first; the correctness long tail continues.
0. **For the human**: the sharing A/B on the laptop (the prompt is notes/sharing_design.md "The laptop A/B") decides
   stage 2; keep or revert the relation HACK's removal (one commit, react's and type-fest's cost); nest's libs-phase
   sys time under the 4 KB-page AST slabs (`TTTT_AST_SMALL_KB=0` against the default).
1. **Small projects**: react is bound by one file (form.tsx, ~44 ms at K1: a per-file cost tsc pays too; check
   tsgo's time on that file); per-checker duplication is the rest.
2. **Code footprint**: still ~50% frontend-bound (notes/code_layout.md, `scripts/inlinedup.py`).
3. **The correctness long tail, continuously**: kebo's mobx-state-tree cycle (lazy parameter types in signature
   resolution: a design first), better-drizzle's two TS2578, mcp-linker's 1.73x types, the indexed-access recursion
   identity's repro, hono; a fifth refresh rule when the human reports new divergences.
4. **Mid-size threading**: stage 1 (content order by default) and stage 2 (the shared graph on for K > 1, the plain
   path at K1) of notes/sharing_design.md, as the laptop A/B decides; stage 3 (rollback) no-go for now.
5. **Re-pin to 7.1.0 stable** when it ships.

## Decisions (each with its reason; the narrative is in git history of this file)
- **Bun's sharing model is work, not incompatible** (2026-10-23, sg2, notes/sharing_design.md): an id-free
  deep-nesting order (bun's content order: height, then push order) costs 2-3 suite tests and ~20-35 real-code
  diagnostics, all from the deep-nesting cut; with it the shared graph is deterministic (160 / 161 loaded K12 runs,
  the miss effect's known residue, against 12 / 20 for today's flag, whose residue C came back with a parser inlining
  change: a latent race); bun-style rollback would recheck 0-10% of the work on our corpora. Layout: the shard's
  slots pointer split from its lock line (-4-5% check CPU under the flag; slot padding and file-end batching lose);
  per-instruction premium drizzle 1.38 -> 1.29x, zod 1.26 -> 1.14, effect 1.16 -> 1.06; net check CPU against plain
  K12 drizzle -11%, zod +4%, effect +10%. The staged path: stage 1 content order by default (go), stage 2 the flag on
  for K > 1 with the plain path at K1 (go after the laptop A/B: the IPC gap, vue's 2x), stage 3 rollback (no-go now);
  a warm prefix + fork() is not worth it. Awaiting the laptop.
- **A semantics commit lands only with a repro or the full source sweep, gated at several checker counts**
  (2026-10-23): the indexed-access recursion identity (tsc's rule, landed without a repro "needed only in combination
  with" the removed HACK) regressed youtube-enhancer, outside the quarter sample (1.16M types against tsgo's 211k, a
  spurious TS2589); the full run caught it and it was reverted (triage.md). c0a04350a (a generic mapped target no
  longer passing an intersection source by its members) was right at K1 and added a TS2502 to better-drizzle only at
  K>1 (kdiff caught it; cor7's acb8dfe7f fixed it): a relation change runs kdiff on the affected project at K1 and
  K8/16, not only the K1 gates.
- **Documented hacks are suspects when a divergence appears** (2026-10-23): two produced wrong answers in a week, the
  deep-nesting target push (pi-bolt) and the generic-mapped member fallback (better-drizzle's 4.23x types runaway
  behind tsc's TS2502). Read hacks.md for the code path before hypothesising.
- **The conditional_source_related HACK's removal is accepted at react +36%, type-fest +7% instructions**
  (2026-10-23, f38a8b493): tttt now does the relation work tsc does on react-hook-form's Path/PathImpl (tsgo 84.6k
  relations / 133k types on react, tttt 93.4k / 89.6k; the old 21.8k / 39.5k was the wrong answer's price) and
  pi-bolt's TypeBox chain is fixed. Price a hack's removal against tsc's work, not the hack's speed; a one-commit
  revert if the human decides otherwise.
- **No persistent thread pool for now** (2026-10-23, fe4): ~1 ms on nest for 1-2 days of work and a 58-thread-local
  audit; the design and the audit are in notes/front_end.md. **The loader not blocking on libs** was built (fe6,
  b6f8a4650, with a `TTTT_DEFER_LIBS=all` oracle against `TTTT_NO_DEFER_LIBS=1`): nest libs + load 41 -> 37 ms on
  this box, the laptop to confirm.
- **Directory listings in CachedFs landed at the +0.5% instruction bar** (2026-10-23; nest +0.45%, syscalls down by
  thousands): nil on the laptop's wall either way (bbe37c5f), kept.
- **The shared type graph is parked** (2026-10-21/22): built correct by construction and in the tree behind
  `TTTT_SHARED_GRAPH=1`, but at K12 each of its instructions costs 1.2-1.6x plain's, which outweighs the duplicated
  work it saves on ordinary projects, and its output still differs under load on effect (~1%). It wins 2.2-2.5x only
  on recycle thrashers (dokploy, monkeytype). Reopened by the sharing study (above): content order and the split
  shard lines answer the determinism and part of the premium; the laptop's counters decide (notes/shared_graph.md). The earlier lock/wait/redo design was deleted, not narrowed: waiting
  creates the cycles a redo must break, and the redo's history is no sequential checker's.
- **The shared graph's ceiling is measured** (2026-10-22, notes/shared_graph.md "The ceiling, measured"): at most
  12% off a vscode checker at K24, below its own 1.2-1.6x per-instruction overhead: off the table for vscode; only
  drizzle-shaped projects (63%) would justify it. The perf-c2c measurement planned for it is withdrawn. The laptop
  measured drizzle at K12 with the flag: -26% instructions but only -9% cycles, wall 0.300 -> 0.273 s.
- **Cross-checker duplication is mostly inherent; no scheduler** (2026-10-22/23, notes/duplication.md): check-phase
  instructions K12/K1 vscode 1.09x (K24 1.14x), effect 1.15x, excalidraw 1.41x, drizzle 2.15x, half to three quarters
  inherent. Import-graph affinity, block stealing and dealing by a per-file cost estimate (bun's identifier count) were
  all measured negative or within 1-2% on vscode: not built (a cost estimate predicts no better than bytes).
  Stealing already ends at the costliest file.
- **Variance sharing is closed** (2026-10-22, `TTTT_VARIANCE_CENSUS`, `scripts/variancecensus.py`): a shared
  variance isn't K-independent by construction, the clean share is under 1% of K12 instructions, and drizzle's
  duplicated variance work is tsc's own per-closure cost (tsgo pays it too); the "25x blow-up" seen
  on the way was a census counter summing type ids across id chunks.
- **Bun's concurrency is not adoptable whole** (2026-10-22, notes/bun_concurrency.md): bun publishes selectively in
  batches and keeps its output deterministic by rollback, which needs id-free type ordering (priced since: 2-3 suite
  tests, ~20-35 real-code diagnostics, the sharing study above).
  What applied: the chunked parse of big declaration files (landed), a persistent pool (designed, declined for now),
  link in parallel per import-cycle level (30-40 ms on vscode; after link's page faults).
  Its type-store shape was studied as a memo (notes/sharing_design.md): the staged path above.
- **GCC, BOLT and huge text** (2026-10-22, the laptop at 8e3f4fb0 and 277be4bc): the shipped build is GCC LTO + PGO
  relaid out by llvm-bolt (-3.2% K1 wall) with the hot text on a 2 MB huge page (-4.6% K1 wall). clang compiles but
  costs +1.4% cycles at K1 and +2..7% instructions, with a 3.67 MB hot window against GCC's 2.47: not the production
  compiler. `-fno-peel-loops` took -1% cycles for +1.2..3.7% instructions: not taken.
- **Names are not interned per program** (2026-10-22, wt-fe2 built it, notes/front_end.md): on the laptop vscode got
  +1.8% wall, +2.8% cycles; cycles rose 5x more than instructions fell. Declined.
- **The parser gets targeted fixes, not a rewrite** (2026-10-22): tttt already parses in 2/3 of tsgo's instructions;
  first-word statement dispatch, skipper tables, quick tests and lists built once took vscode's parse -14%
  (notes/front_end.md "The parser"). Dispatch shortcuts land with a `VERIFY_GATES` build that checks the fast path
  against the slow one.
- **Checker constant-factor mining is at its floor** (2026-10-22, notes/checker_speed.md): sites now give 0.1-0.5%
  each; stop mining. Lazy message formatting (2.6% on drizzle) waits on hacks.md's "Two circularity diagnostics come
  from printing a type for its side effects" being fixed tsc's way.
- **Every unfixed divergence gets a known-failing test** in `test/cases` (2026-10-22; the results file records
  pass/fail per component): the real-code oracles are down to confirmed tsgo artifacts and written-up designs.
- **Declined with write-ups** (2026-10-22): a heuristic reconstructing tsc's deferred-body check order and arrayFrom
  (bugs/missing_checks.md), import-graph affinity and block stealing (notes/duplication.md), spinning on the pool
  lock, skipping display for suppressed errors, a member_refs memo (notes/checker_speed.md).
- **No shared declared layer** (2026-10-18): what checkers re-derive is instantiations of the project's own code; lib
  and `.d.ts` declarations are 2-6% of it (measurements.md "What the checkers re-derive").
- **Defaults** (2026-10-22/23): `--threads` every core; checkers one per 16 KB of checked source (was 64 KB: nest and
  react get ~8 checkers, nest -20% wall at +13% RSS; level with K12 on the laptop, ahead of K4 on nest, xstate and
  valibot), capped at ceil(checked bytes / the largest checked file) (10398e204: TypeScript6 8 -> 4, where the laptop
  measured K4 -10% wall and -32% CPU), up to the physical cores, past them one per 4 MB up to the threads (only vscode gets 24); `--checker-memory`
  a quarter of RAM (256 MB..8 GB; zero recycles on everything measured); a stolen file starts a fresh checker only
  when the thief holds half its budget. A wall gain must be weighed against CPU: K12 -> K24 on vscode bought 0.09 s
  for 12 CPU s.
- **Memory is secondary until we are faster** (the human, 2026-10-22: the goal is distinctly faster than bun canary;
  up to ~3x bun's peak is fine for a clear speed win). No budget knob: on vscode (16 threads, loaded) the default's
  1.20 s / 5.15 GB against 1 GB's 1.39 s / 3.1 GB; recycling costs 15-20% of check time and ~2 GB of the peak is
  budget-independent (measurements.md "Memory budget sweep on vscode"): structural, deferred. Closed on the way: the compact AST (2026-10-16: vscode's zero words are diffuse, exact-size variants reach
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
  serialized CPU. Walls and every cycles-level claim come from the human's laptop (cachegrind's icache proxy
  over-predicted `-fno-peel-loops` and under-predicted BOLT). The shipped build is `scripts/fastbuild.sh` (State).
- **The parser's error recovery needs no rewrite** (2026-09-29: one list/commit-point framework).
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
Hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks), tsc-shaped binder with
persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity relation, stable type
ordering, mapper instantiation, tsc's inference flow (return mappers included) and ContextChecked/context anchors,
one circularity stack with resolution barriers, conditional/mapped/indexed/template/substitution types, deferred
references, flow graph + narrowing, module resolution + tsconfig with tsgo's two-phase file collection, iteration
protocol, generators, `this` types, mixins, decorators, union origins, elaboration, JSX, tsc's type printer and
symbol chains, the pseudochecker, JSDoc + CommonJS + expandos as 7 has them, parallel loading / chunked parsing of
big declaration files / declaring / resolving / checking with the output independent of the checker count, work
stealing, checker recycling, deferred function bodies, per-file AST arenas, mapped source text, declaration
diagnostics, the declaration writer with comments and maps for TypeScript and JavaScript, project references / `-b`
through emitted declarations, 7.1's content mappers, tsc's laziness and deferred checks, 7.1's language, in-flight
state as per-checker records, one atom table and one type-id space per program, the shared graph behind its flag.
~150k lines of C.

## Absent or approximated
Pseudochecker gaps (bugs/display.md "Pseudochecker gaps"), namespace-import clones (cloneTypeAsModuleType), member
identity (members aren't Symbols), tsc's re-entrancy beyond the declaration and return-type sites
(notes/reentrancy.md), errors from a loop's intermediate flow types beyond the partial pass's (its K-dependence hole:
triage.md), the CLI modes above.

## Architecture assessment
A clean, direct-style checker: hand-written backtracking parser straight from `char*` (no tokenizer, a fail cache for
the ambiguous cases), an AST of plain structs in per-file arenas, a binder (persistent symbols and scopes, merging
like tsc's), then checking driven by `LazyType` (memoised, cycle-detecting per-node types) over the interned `Ty`
core. All checker state is in `Checker`; the AST is read-only while checking. What's left to watch:
- **Parser**: unbounded recursion depth (binary chains need ~8 MB of stack; tsc walks them iteratively); speculative
  attempts copy a `Parser` (child parsers on vscode 73M -> 41M after the round), no token stream: cheaper than tsgo's
  already, so targeted fixes. The debug build's suite aborts at a parser check (triage.md).
- **Binder**: declare runs in parallel for files that declare only into their own tables (96% of vscode's files);
  link is serial (alias resolution's order is observable; bun links in parallel per import-cycle level, worth it
  only once link is real work after its page faults: notes/bun_concurrency.md, notes/front_end.md); the resolve walk shards per file, big declaration files in chunks
  (hacks.md "Parallelism contract").
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
- **Competition**: bun canary is within 2% of tttt's wall on vscode (1.74 against 1.70 s) at 40% of its memory: the
  lead is not yet "distinctly faster"; tsc-rs (a Rust tsgo
  port at our pin, `scripts/bench.sh --tscrs`) spends the least CPU at few checkers. Our margin is single-threaded
  efficiency, which many cores spend on all-core clocks, SMT and duplicated work.
- tsgo is order dependent in places tttt isn't; tsgo 7.1's ordering rules are what `ty/order.c` must port, not patch.
