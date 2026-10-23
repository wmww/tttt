# Vision

## Goal
A complete TypeScript typechecker in C, **distinctly faster than bun canary's `bun check`** (the fastest competitor:
measurements.md "The 12-core box at 3a77c827"), with typescript-go 7.1 (tsgo) as the oracle for every diagnostic,
type and emitted declaration. Restated by the human on 2026-10-22: speed first; memory is secondary until we are
faster; correctness is never traded. Completeness first in the sense that nothing is built on a foundation that can't
be fast or can't be complete. The suite's baselines are the first oracle, not the goal; real projects that are clean
under tsc are the second.

## State (2026-10-23, the fourth coordinator's end: oracles at d7bc837b0, main past 8d4518ecc; the laptop at 3a77c827)
- **Target: TypeScript 7.1** (`main` at the 7.1 nightly fed0bf24, `thirdparty/typescript`; re-pin to 7.1.0 stable
  when it is on npm: todo.md). Suite with own tests and the dts component **13999 / 14035** (dts 1888 / 1910), the
  same with `--no-defer-bodies`, `--checker-memory 0` 0 of 17672 differ; two of tttt's own tests fail by design
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
- **Speed: the first win validated on the human's private project set.** The kernel-time round (stack pool, glibc
  `M_MMAP_THRESHOLD` 32 MB / `M_TRIM_THRESHOLD` 1 GB, source texts kept mapped, huge-page big tables:
  notes/kernel_time.md) made **330a6eb97 more than 10% faster than 1c31b0469 on most of the human's private
  projects** ("the biggest validated win in a long time"). Before it, the private set moved only 0.98 over BOLT, the
  huge text page, the parser's -14% and the front-end round: those were bench-fitted (Decisions). On the laptop
  (measurements.md "The 12-core box at 3a77c827"): kernel cycles **21-28% of user -> 6-9%** on drizzle, zod, effect,
  vue and vscode, TLB shootdowns per run **-95..-99%**, faults -79..-96%, walls **-13..-23%** on every mid and large
  corpus (react -16%, nest level: start-up bound); **tttt beats bun canary 1.4.4 at the defaults on every bench
  corpus, vscode 1.75 against 1.93 s** (vscode median 1.70). Peak RSS rose +4..15% for it (accepted: speed over
  memory). Single-threaded tttt is fastest everywhere (vscode 9.6 s against bun's 12.9). Where the human's private
  projects still lose to bun, tttt uses less total CPU on every one: the losses are critical path (idle cores), not
  duplication (notes/bun_breadth.md, the breadth sweep not yet run).
- **The exit tail** (6d7cc707e; the measuring scripts count the teardown again with 29c1119ef):
  `exit_detached()` (main.c) hands the address space to a detached CLONE_VM reaper so the caller sees the exit
  before the kernel frees 1-5 GB of pages: the 7-66 ms tail (3-6% of wall) goes to ~0 (drizzle 0.290 -> 0.278,
  vscode 1.93 -> 1.81 on this box). Its sys time is then nobody's: `TTTT_SYNC_EXIT=1` for honest sys (hacks.md).
  Its follow-up, 2 MB-aligned huge arena chunks (97839044a): faults drizzle 72k -> 44k, vscode 301k -> 185k, sys
  -12..-20%, RSS +0..2% (measurements.md "2 MB-aligned huge arena chunks").
- **The shipped build is `scripts/fastbuild.sh` -> `fast/tttt`**: GCC LTO + PGO, `-fno-plt`, `-z now`, then an
  llvm-bolt layout (llvm-bolt is a dependency), the hot text on a huge page at startup (`huge_text()`, verified via
  smaps_rollup, with an `MADV_COLLAPSE` retry; `TTTT_HUGE_TEXT=0/2`). The measuring scripts take their binary from
  `fastbuild.sh --measured`. `release/` (plain -O3) stays the edit-test build.
- **Memory**: vscode 5.5 GB at the defaults against bun's 2.2 (K1 4.3); the kernel round added +4..15% everywhere,
  the small projects most in relative terms (nest +44 MB). The human now calls it "a lot" and worth cutting where it
  is cheap (Decisions: three levers). Recycling costs 15-20% of check time on vscode at small budgets and ~2 GB of
  the peak is the program plus per-thread state whatever the budget.
- Instructions: `test/instrcount.txt` (fast/tttt, every corpus, at d7bc837b0): **vscode 66.37 G**, flat through the
  kernel round (kernel work isn't in callgrind: its win is invisible to this oracle); react's +36% and type-fest's +7%
  of round three stand (tsc's relation work: Decisions).

## Direction (2026-10-23, the fourth coordinator's hand-off; the items and their numbers are in todo.md)
The human's priorities: distinctly faster than bun canary on real projects, judged on their private set; speed over
memory, but memory is now worth cutting where it is cheap; easy, measured wins first; correctness never traded.
0. **For the human**: the laptop asks (remaining kernel cycles, the exit tail under THP `always`, the recycle-budget
   sweep, the bench on main after the reaper); keep or revert the relation HACK's removal.
1. The **breadth bun sweep**: tttt against bun canary on the ~500
   heaviest source-sweep projects, the worst dissected by `scripts/perfshape.sh`. That is where the human's remaining
   losses live, and the expected remedies are policy and scheduling (more checkers when the critical path is long,
   splitting what one dominating file holds, checking earlier).
2. **Memory**: the recycle-budget default from the laptop sweep, per-file release if the census says it is large,
   phase-boundary trims.
3. **Kernel round three** from the laptop's `cycles:k` profile.
4. **The correctness long tail, continuously**: kebo's mobx-state-tree cycle, better-drizzle's two TS2578,
   mcp-linker's 1.73x types, the recursion identity's repro, hono; a fifth refresh rule when the human reports new
   divergences.
5. **Small projects**: react is form.tsx-bound (tsc too), nest start-up bound (the front end 2/3 of 55 ms).
6. **Re-pin to 7.1.0 stable** when it ships.

## Decisions (each with its reason; the narrative is in git history of this file)
- **No machine-specific tuning** (2026-10-23, the human): no pinning of work to kinds of cores, no `-march`, no
  per-microarchitecture rules; the shipped build's PGO and BOLT profiles are instrumentation counts of the training
  corpora (input-dependent, not machine-dependent) and the huge-page paths fall back at runtime. Defaults are derived
  from arguments that hold on any Linux x86 box (core counts, bytes, file sizes, RAM); the laptop confirms a
  mechanism, it does not suggest a rule. The hybrid-CPU placement experiment is withdrawn (todo.md).
- **Perf work is judged by mechanism or breadth, not by the bench** (2026-10-23): the human's private project set is
  the judge, and before the kernel round it moved 0.98 over a stretch of bench wins (BOLT -3.2% K1 wall, the huge
  text page -4.6%, the parser's -14%, the front-end round), which were fitted to the 15 bench corpora. The kernel
  round won >10% on most private projects because its argument was a mechanism (shootdowns and faults at the
  defaults, present in every multi-threaded run), not a corpus. A perf change lands on a mechanism argument or a
  breadth check (perfsweep, the bun breadth sweep, `scripts/perfshape.sh` reports the human can paste), and its
  claim is stated as "where else this applies".
- **The kernel and OS layer is a bottleneck this VM can't see** (2026-10-23): the shared 16-vCPU KVM box has THP
  `madvise`, other agents' shootdowns in `/proc/interrupts` and no perf; on the laptop (THP `always`, idle) the
  same changes cut faults -79..-96% where this box saw -8..-27%. Kernel cycles, faults, placement and anything
  cycles-level are measured on the laptop with `perf stat` / `perf record -e cycles:k`, and sys is compared with
  `TTTT_SYNC_EXIT=1` (the reaper hides the teardown's sys otherwise).
- **Sharing the type graph is closed** (2026-10-23; notes/sharing_design.md, measurements.md "The 12-core box at
  48cd9835"): bun's model is work, not incompatible (content nesting order makes the shared graph deterministic,
  160 / 161 loaded runs), but on the laptop at K12 it wins only on drizzle (-14% wall) and loses on zod (+2%),
  effect (+11%) and vue (+25%): the IPC gap of shared lines (plain against the best layout: drizzle 1.40 -> 1.24,
  zod -7%, effect -4%, vue -9%; 4-11%) outweighs the instructions saved everywhere but drizzle-shaped projects, and
  content order moves 3 of drizzle's elaboration chains away from tsc's text. Stages 1-3 are not built; the flag
  stays in the tree, parked. The per-checker model is at the hardware's ceiling for tsc's algorithm; the absolute
  wall is what matters and it leads bun.
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
  on recycle thrashers (dokploy, monkeytype). The sharing study (above) answered the determinism and part of the
  premium and the laptop's counters closed it (notes/shared_graph.md). The earlier lock/wait/redo design was deleted, not narrowed: waiting
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
  valibot), capped at ceil(checked bytes / the largest checked file) only when that file is >= 1 MB (10398e204,
  8d4518ecc: TypeScript6's 3.15 MB checker.ts 8 -> 4, where the laptop measured K4 -10% wall and -32% CPU; type-fest,
  tanstack-router and react are not bounded by one file's bytes: type-fest's K4 was 0.28 s against K12's 0.18), up to the physical cores, past them one per 4 MB up to the threads (only vscode gets 24); `--checker-memory`
  a quarter of RAM (256 MB..8 GB; zero recycles on everything measured); a stolen file starts a fresh checker only
  when the thief holds half its budget. A wall gain must be weighed against CPU: K12 -> K24 on vscode bought 0.09 s
  for 12 CPU s.
- **Speed over memory, but memory is now worth cutting cheaply** (the human, 2026-10-22/23: up to ~3x bun's peak was
  fine for a clear speed win; at vscode 5.5 GB against bun's 2.2 it is now "a lot"). The kernel round's +4..15% RSS
  was accepted for its walls. Three levers, in order of price: (1) the `--checker-memory` default (a quarter of RAM;
  the laptop's 4096 / 3072 / 2048 sweep on vscode and drizzle, wall against RSS, decides it; on this box 1 GB was 1.39
  s / 3.1 GB against the default's 1.20 s / 5.15 GB); (2) per-file release, dropping a checker's per-file results
  (expression, flow, node memos, per-file relation work) at the file's end as bun does (most of its 2.2 GB on vscode):
  structural: wt-memc's census found 82% of a zod checker's arena droppable at file end, mostly instantiated types,
  but reaching it means copying survivors out, the copying recycle's design (todo.md 3, notes/memory.md); (3)
  phase-boundary trims (small: a trim burst when no checker runs). ~2 GB of vscode's peak is budget-independent.
  Closed on the way: the compact AST (2026-10-16: vscode's zero words are diffuse, exact-size variants reach ~165 MB
  at most), the copying recycle (2026-10-17: costs about what re-derivation does), further struct diets (< 120 MB
  left), freeing ASTs after their file (other files' checks read them).
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
- **Competition**: tttt leads bun canary 1.4.4 at the defaults on every bench corpus (vscode 1.75 against 1.93 s,
  ~10%) at 2.5x its peak memory, but the human's private set still has projects where bun wins (on wall, never on
  total CPU): the lead is not yet "distinctly faster" everywhere, and the breadth sweep hasn't measured how many
  projects that is. tsc-rs (a Rust tsgo port at our pin, `scripts/bench.sh --tscrs`) spends the least CPU at few
  checkers. Our margin is single-threaded efficiency, which many cores spend on all-core clocks, SMT, idle cores
  behind one dominating file and duplicated work.
- tsgo is order dependent in places tttt isn't; tsgo 7.1's ordering rules are what `ty/order.c` must port, not patch.
