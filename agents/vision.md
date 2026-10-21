# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-10-21, after the coordinator day)
- **Target: TypeScript 7.1** (`main` at the 7.1 nightly fed0bf24, `thirdparty/typescript`; re-pin to 7.1.0 stable
  once it is on npm, expected after 2026-11-24: plan 58; 7.1.0-beta, published 2026-10-20, is not a re-pin target).
  Its native suite: 13556 / 13568 (99.9%), the last 12 off the table (argued or one-test mechanisms,
  bugs/tsgo_divergences.md); with own tests and the dts component **13958 / 13992** (dts 1887 / 1909). Every oracle
  cache is tsgo 7.1's, every probe runs tsgo (`test/oracle`: a Go overlay, so a patched tsgo is one `go build` away).
- **Drop-in `tsc --noEmit`, and drop-in `tsc` where the config only emits declarations**, TypeScript and JavaScript
  sources alike (plans 53, 64). **clidiff** (the whole `--pretty false` output, byte for byte, every corpus and
  source sweep project): **3824 / 3831**. dtsdiff **1327 / 1350** projects (1330 on 2026-10-20; part of the rest is
  tsgo's own checker split). JS emit is out of scope (the human, 2026-10-12). **Refused**: `--watch`,
  `--incremental`'s `.tsbuildinfo`, `--showConfig`, `--explainFiles`, `--init` (plan 76, parked by the human
  2026-10-16: stays parked unless reopened).
- **Real code**: corpora 0 / 0 everywhere but vue (0 / 2, plan-sized: tsc's in-flight-free member resolution), **DT
  sweep 8595 / 8595**, `$ExpectType` 68640 / 68640, flagflip 18 / 3 / 2, scenarios 221 / 0 failed, **source sweep
  5235 / 5242 projects** (every `tsconfig*.json`; 2026-10-21's G1 run sampled a quarter: 1272 / 1274), **`-b` sweep
  411 / 416 solutions**, kdiff 4, recycle 4 (all tsgo-shared).
- **Perf and memory at the defaults** (12-core box, PGO + LTO: measurements.md "The 12-core box at efb91998"): tttt
  is fastest single-threaded everywhere (1.1-2.9x bun canary, vscode 10.4 s against tsc-rs's 14.0) and uses the least
  CPU at the defaults but tsc-rs's 4 checkers on 3 / 14; bun canary beats tttt's default wall on 4 / 14 (vscode 2.05
  against ~2.5 s, drizzle, excalidraw, nest) at 1.7x tttt's CPU, and bun's peak is lower on 10-12 / 14. Today's K12
  spends 2.3-4.4x K1's CPU: clocks 1.62x, duplicated instantiations ~1.2x (drizzle 1.4x), recycling 5-8%.
- **`--shared-graph` (plan 86 stage 4d, `src/ty/graph.c`, `src/ty/shared_table.c`; `TTTT_SHARED_GRAPH=1`)**: one
  type graph shared by every checker on real threads. Identity kinds are published at creation, memo kinds when
  their value exists, Ty-resident lazy states are claimed by CAS and waited for; a broken wait cycle redoes the
  yielder's file in a fresh checker; the lib's hot types are warmed before the workers start; variances have one
  shared slot per subject and, for now, one measurement at a time (a graph-wide lock). **Gate G2 (measurements.md
  "Plan 86 gate G2", a loaded 16-vCPU box)**: K12 work 1.10-1.13x K1 (was 1.4-2.6x), K1's output at K4 and K12 on
  every `-p` corpus (drizzle 300 / 300 under load, vscode 5 / 5), CPU lower than today's K12 on 10 / 14, wall equal or
  better on 11 / 14 (worse on drizzle: the variance lock; type-fest, effect), **peak RSS higher everywhere** (nothing
  is recycled: vscode 4.4-4.6 GB against 1.4). Not the default; stage 4e makes it one.
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, stable type ordering, mapper instantiation, tsc's inference flow (return mappers included) and
  ContextChecked/context anchors, one circularity stack, conditional/mapped/indexed/template/substitution types,
  deferred references, flow graph + narrowing, module resolution + tsconfig with tsgo's two-phase file collection,
  iteration protocol, generators, `this` types, mixins, decorators, union origins, elaboration, JSX, tsc's type
  printer and symbol chains, JSDoc + CommonJS + expandos as 7 has them, parallel loading / declaring / resolving /
  checking with the output independent of the checker count, checker recycling, deferred function bodies, per-file
  AST arenas, declaration diagnostics, the declaration writer with comments and maps for TypeScript and JavaScript,
  project references / `-b`, 7.1's content mappers, tsc's laziness and deferred checks, 7.1's language, in-flight
  state as per-checker records, one atom table and one type-id space per program. ~150k lines of C.
- **Absent or approximated**: pseudo-type builder gaps (plan 55's list), namespace-import clones
  (cloneTypeAsModuleType), member identity (members aren't Symbols), the CLI modes above.

## Course correction (2026-10-21, after the coordinator day)
- **The workflow changed**: a coordinator runs 3-4 Opus workers in worktrees in parallel and merges their commits
  (CLAUDE.md "Workflow"). In ~14 hours ~70 commits landed: what was scheduled as two windows (4a's three sessions,
  then 4b-4c to G1) plus 4d's 3-4 sessions, stage 3's first session and the IntersectionState audit. Parallel workers
  pay where the work splits along files or stages with disjoint code; the shared state is the oracles' caches and the
  machine's load (walls measured today are noisy: work is measured in instructions).
- **Plan 86 stage 4 passed both remaining gates.** 4a (in-flight state into per-checker records; plan 88 stage B
  adopted: declarations and return types re-enter below the call barrier), 4b (M1 fixed, M2 a fingerprint artifact,
  the write-after-publish sites), 4c (one atom table per program, global type ids with a directory, first-touch stamps
  behind `--first-touch-stamps`) were K1 refactors with every oracle byte-identical: **G1 passed** (a full
  `scripts/oracles.py` run matched the session-start numbers, but for two `-b` crashes from the oracle scripts'
  address-space cap, since raised). 4d built `--shared-graph` on threads and **G2's work
  and determinism criteria passed; its memory criterion failed** (no recycling with the flag). Determinism took the
  most: a lookup race in the shared tables (the segfault and most flakes), broken wait cycles (fixed by the redo, not
  by computing through them) and concurrent variance measurements (fixed by a lock whose need isn't understood yet).
- **Decision: the next window is stage 4e** (plan 86 "Stage 4e", ~9-13 sessions, in its order): narrow the variance
  lock (understand what one measurement reads from another first), the shared relation cache (designed and
  measured: 7-40% of K12's relation work, 0 disagreements), the arena split into a published and a recycled private
  part (memory: vscode K12 within ~1.3x of today's 1.4 GB), the redo's gaps, then the whole oracle set with the flag
  and the default on where it wins. The human's 12-core laptop bench (with and without the flag) decides the default
  policy, not this loaded box. Plan 58 still goes first if 7.1.0 is on npm.
- **Stage 3 (front end) is done to where it pays**: sharded resolver caches, the loader never stats or frees the
  workers' blocks, a rank-ordered load pool, parallel declare (10604 of vscode's 11015 files), module refs on threads,
  a word-at-a-time symbol hash. The import pre-scan waits for a quiet 12-core measurement (`TTTT_LOAD_POOL_INLINE=1`);
  link stays serial by design (plan 86 stage 3: < 1.5% of vscode's wall for the binder's riskiest code).
- **IntersectionState stickiness is fixed** (structured_related_impl() clears both states, passing() hands them on
  at tsc's sites; suite +1). Two neighbouring divergences went to triage.md.

## Course correction (2026-10-20, condensed 2026-10-21)
- Plan 86 stage 4 (one type graph shared by every checker) went ahead of stage 2: the human asked for multiples on
  many cores, bun canary wins default wall on 4 / 14 by keeping ~20 threads busy, and only a shared graph makes more
  checkers pay and cuts memory at the defaults. Its design said go, staged (~25 sessions); G0's taint prototype said
  go. The rule "faster than bun on every corpus at the defaults" stays the target and stage 4 is the plan to restore
  it, not another tuning of the defaults' policy (five were declined along one frontier).
- Every staged refactor before sharing (4a-4c) had to leave every oracle byte-identical, so a later no-go would leave
  better-structured code, not a half-feature: that held. Plan 88's stages A-B folded into 4a; C-E stay in plan 88.
- PGO + LTO took 11-17% instructions on every corpus. Stage 2's remaining levers (instruction footprint, intern table
  layout) can't be confirmed without hardware counters: perf slots, measured on wall at K1. On 12 cores
  costliest-file-first is a wash and the front end is ~0.5 s of vscode's 2.5 s.
- `-b` has one model (plan 87 stage 1: emitted declarations); its leftovers are `-p` checker bugs
  (bugs/sourcesweep_build.md). Correctness is in its long tail and holds; each window keeps one real-code session.

## Course correction (insights of 2026-10-18 and 10-19, condensed 2026-10-21)
- Correctness work comes from refreshes and new oracle axes, not the existing oracles' tails (day-one rates 96.1,
  97.1, 98.1%: the checker generalizes); "saturated" oracles hid a whole axis (message text), so every oracle that
  compares a projection should say what it doesn't compare. Debt windows after feature windows are cheap.
- Plan 85 (a shared *declared* layer) closed no-go: what checkers rebuild is instantiations of the project's own
  code, hence plan 86 stage 4's whole shared graph. tsc-rs is tracked next to bun (its edge: CPU at few checkers).
- Price many-core work by instructions (callgrind) and by per-file time sums under real parallel runs on a quiet box,
  never by serialized CPU; on the shared 16-vCPU box per-file wall sums at 12 threads aren't work (G2).
- A drop-in must survive its inputs changing mid-run (`text_sigbus()`).

## Decisions still in force (from earlier course corrections; details in git history of this file)
- **The suite's last 12 are off the table** (2026-10-15): each is argued (bugs/tsgo_divergences.md) or a plan-sized
  mechanism for one test; work them only if real code shows the mechanism or it falls out of other work. Fidelity to
  tsgo *artifacts* (a runaway's model, a cache collision) is special-casing in a respectable form: don't.
- **Memory target** (2026-10-16, the human): less memory than every competitor (bun, tsgo) in most cases, as fast
  as possible, correctness unchanged; features (watch, incremental, cheap CLI modes) later. Met as far as the rules
  allow on 4 vCPUs (2026-10-18): defaults 8 / 14 at or below bun, K1 6 / 14, all within 1.2-1.4x, tsgo everywhere;
  **not on 12 cores** (bun lowest on 10 / 14 at the defaults: the many-core policy; plan 85 closed). A memory
  change costs at most ~3% wall at K1; at the defaults the bound is faster than bun on every corpus and the sweep's
  tail (plan 81 stage 1's trade, confirmed 2026-10-18; reverting is `SHARED_CHECKER_MEMORY_MB` at 96). Measure as
  `/usr/bin/time` peak at K1 and at the defaults next to bun's, every corpus (`scripts/bench.sh --bun`); the 12-core
  run is the human's.
- **Refresh the breadth oracles** (2026-10-16): a saturated sweep measures nothing; every few windows add repositories
  chosen by a different rule and report the day-one match rate (2026-10-17: 96.1%; 2026-10-18, declaration-emitting
  libraries and checkJs projects: 97.1%, dtsdiff 34 / 41 projects; 2026-10-19, `tsc -b` solutions and decorators:
  98.1%, and a `-b` oracle, `sourcesweep.py --build`, 394 / 416 solutions: the `-p` sweep only walks `tsconfig.json`,
  so `tsconfig.build.json`-style configs and downstream projects were never checked; the recipe is
  repos.txt's "Plan 77" comment, the steps measurements.md "Plan 77": append rows, `thirdparty/ensure-sources.sh
  NAMES`, `scripts/sourcesweep.py --tsc-new`, `scripts/dtsdiff.py --tsc -f RE`, then both with `-f '^(names)(/|$)'` before
  fixing anything; candidates from `scripts/reposearch.py`).
- **The type count is the perf oracle** (2026-10-11): tsgo's `--extendedDiagnostics` `Types`/`Instantiations` are
  machine-independent and directly comparable with tttt's; a ratio far above 1 is a missing cache or a runaway, found
  without timing anything on this shared 4-vCPU box (`scripts/perfsweep.py`).
- **Thread defaults** (2026-10-12, since 2026-10-18 for the cap): `--threads` defaults to every core, `--checkers` to
  one per thread, at most one per 64 KB of checked source, and **at most one per physical core** (but every thread up
  to 4): 24 threads oversubscribed spend 6-42% more CPU at K24 than K12 for 0-11% wall (measurements.md "Checkers per
  physical core"); the CLI caps the checkers' budget at 1.5 GB together (plan 75). tsgo's output is K-independent and
  so is tttt's (kdiff: only tsgo-shared dependences). The shared graph's default (4e) revisits this policy.
- **Fan-out merge discipline** (2026-10-13): full source sweep match-set diffs and flagflip against a session-start
  run at every fan-out merge (they caught 11 lost projects and a -31 TS6196 nothing else saw).
- **Fan-out sessions** (four Opus sub-agents in worktrees, one failstats/displaydiff/jsstats bucket each, disjoint
  test lists, 0 lost enforced at merge) are the engine for the long tail: now for the re-pin's new failures and
  real-code buckets (dtsdiff, source sweep), not the suite's last dozen. **Instruction counts
  at every session end** (`scripts/instrcount.sh`): creep is silent otherwise.
- **Whole features before per-test bugs** now that the tail is thin: a planning session prototypes and may say no
  (plan 47: prototype the deletion of a hack before designing the mechanism to replace it).
- **Oracles must test both directions**: clean corpora only show spurious diagnostics; flag flips and mutations (plan
  46) show missing ones. Rerun all of them after checker changes.
- **Every todo window has corpus sessions** (the corpora were once left alone for ~30 sessions), and todo.md has a
  fixed session budget that overruns come out of; ending a turn ends the session, so commit before long jobs.
- **Parser: no rewrite** (plan 27 looked; recovery is one list/commit-point framework plus patching). Revisit only
  with a profile. **JS must lower into the same `Ty` machinery**, no parallel JSDoc type world.
- **Real projects are the second oracle.** A rising pass count with flat corpus counts means we're test-chasing.
  Rank corpus diagnostics by root cause, not by code (one mapped type with an `as` clause was 810 TS7006). Every
  insights session looks at the corpus numbers next to the pass count. Corpus root causes that are type-core rules go
  ahead of suite plans.
- **A clean corpus only finds spurious diagnostics**: code tttt doesn't check at all (decorators) looks perfect there;
  missing checks are found by the suite. Pick corpora for the front ends and features they exercise.
- **A relation that is too lenient hides inference bugs**: when relations get stricter, expect a cluster of latent
  inference bugs (plan 19 R1, the callback-parameter rule). Budget for them.
- **Prefer fixes that move whole categories** (`scripts/failstats.py`, `scripts/displaydiff.py`, the corpora); 13k exact-match
  tests invite special-casing message text.
- **Staging in place works** (the type core migration: 8 sessions, suite runnable throughout, scaffolding deleted).
  The expensive part each time is what the correct rule stops forgiving in the remaining approximations. Don't
  transliterate weak algorithms onto a new foundation; write them the way tsc observably behaves. When a checker
  feature "doesn't work" on lib types, look at what the lib *parsed* to first.
- **A small perf session every few plans**: every check costs a little (plan 15's ~4%). Single-threaded tttt must
  stay ahead of single-threaded tsgo before parallelism is worth building. Measure perf at breadth (`scripts/perfsweep.py`:
  type-count ratios against tsgo), not only on the corpora perf sessions have tuned.
- **Default lib files are checked like tsgo checks them** (unless skipLibCheck/skipDefaultLibCheck): it costs ~0.3 G
  instructions per esnext+dom program (~40 ms; tsgo spends ~150 ms on the same), and it found a real checker bug. Lib
  errors are dropped only for unchecked lib files, never emulated at user declarations.
- **The oracle is tsc as it ships, and we follow it** (plans 48, 53, 54): no dual-version modes, delete what the
  oracle deleted, re-pin when a release lands (plan 58). The oracle tools are tsgo built from source with a Go
  overlay (`test/oracle`), so instrumenting tsgo stays one `go build` away.

## Architecture assessment
A clean, direct-style checker: hand-written backtracking parser straight from `char*` (no tokenizer, fail cache for
the ambiguous cases), an AST of plain structs, a binder (persistent symbols and scopes, merging like tsc's), then
checking driven by `LazyType` (memoised, cycle-detecting per-node types) over the interned `Ty` core. All checker
state is in `Checker`; the AST is read-only while checking.

The five foundational problems the first insights found are all fixed: value-semantic types (-> interned `Ty`),
string-keyed relation caches (-> id-keyed), no control flow analysis (-> plan 05), scopes discarded after resolution
(-> plan 04), checker state in the AST (-> per-`Checker` slots). What's left to watch:
- **Parser**: error-recovery fidelity is essentially done (plan 27: 34 tests still differ, 2026-09-29; the framework held, no rewrite needed), unbounded recursion depth (binary chains need ~8 MB of
  stack; tsc walks them iteratively), backtracking cost.
- **Binder**: declare runs in parallel for files that declare only into their own tables (`declares_in_isolation()`:
  96% of vscode's files; libs and global-merging files stay serial); link is serial by design (alias resolution's
  order is observable: plan 86 stage 3 has a two-phase design, not worth it now); the resolve walk shards per file,
  JavaScript too (plan 45; merged type parameters still walk serially; `hacks.md`, "Parallelism contract").
- **Threading**: checking is threaded (stage 3; nothing in the checker had to change once stage 2's audit fixes were
  in, which is evidence the "AST read-only while checking" contract holds). Any new checker-wide memo that *reports*
  must be tagged `Error.once_key`, and anything written to AST/binder state during checking is a race (the frozen
  guards log it; TSan on the corpora is the net: documentation.md "Typecheck" has the build line). Loading is
  threaded too (a cache-warmer pool beside the unchanged sequential loader: `src/load_pool.c`; the shared state is the
  resolver's caches, sharded 64 ways). Name resolution shards per file (plan 45 stages 1-2).
- **The shared graph** (`--shared-graph`, plan 86 4d): the checker is no longer private state per thread under the
  flag. Every Ty-resident lazy field is claimed by CAS, written once and read through a snapshot; identities and
  memo entries are published into sharded lock-free tables; in-flight state lives in per-checker records (4a). A new
  lazily written field must follow that protocol (plan 86 "Stage 4d"), or it is a race only TSan and the 20-run
  tables find.

## Risks / concerns
- Exact-match oracle: `types`/`symbols` baselines require tsc's type printing (alias preservation, union order,
  origins, truncation). Union order is tsc 6's compareTypes now (`ty/order.c`); type ids still break its last ties
  (fresh/regular literals, same-symbol anonymous types, conditional roots), so creation order still matters a little.
- Perf: deep generic code is where tsc's caches and shortcuts matter (the 2026-09-27 check-up's wins were all tsc
  caches tttt lacked: key-property maps, reverse mapped types, containsType). Checks accumulate cost silently between
  perf sessions (+7% over 09-25..09-27). Wall times drift with the machine: compare callgrind counts across sessions.
- JS/JSDoc is a second front end on top of the checker: the risk is a parallel type world for JSDoc. It must lower
  into the same `Ty` machinery.
- tsgo is order dependent in places tttt isn't; tsgo 7.1's ordering rules are what `ty/order.c` must port, not patch.
- **Competition**: bun check (a Rust tsgo port, 2026-10) beats tttt on memory at the defaults
  on many cores; tsc-rs (pingdotgg/ts-rust, a Rust port of tsgo at our pin, `scripts/bench.sh --tscrs`) spends the
  least CPU at its defaults and is tsgo-exact. tttt is fastest single-threaded by 2x+ but bun canary wins default
  wall on 4 / 14; our margin is single-threaded efficiency, which the defaults spend on SMT inflation, recycling and
  duplicated instantiations. The shared graph removes the duplication; whether it wins wall on the 12-core box is
  the human's bench, not yet run.
- **Determinism vs scaling**: the output must stay byte-identical at any checker and thread count, so across machines
  (`scripts/kdiff.py`, `scripts/checkerdiff.py`, `scripts/threadcheck.sh`; `scripts/oracles.py` runs kdiff). **Under
  the shared graph this is a property of the schedule, shown by repetition, not by construction**: the 20-run tables
  and drizzle's 300 runs passed on a loaded 16-vCPU box, a quiet many-core one hasn't run them. Three mechanisms carry
  it and each is a soundness argument still open: the redo (a yielder's file checked again in a fresh checker is
  assumed to reach K1's history; its gaps: deferred-body materialization, leaked temporaries), the variance lock (it
  removed a ~2% flake whose mechanism is unknown: narrowing it without understanding it brings the flake back), and
  the steady tainted results every history shares (G0). Only the suite and the corpora have run with the flag; the
  sweeps, kdiff and the rest must run with `TTTT_SHARED_GRAPH=1` before it is the default.
- **Memory under the shared graph**: nothing is recycled with the flag (a checker's arena holds both what it published
  and its scratch), so the defaults' memory target ("Decisions still in force") is further off with it than without:
  vscode 4.4-4.6 GB at K12 against 1.4. 4e's arena split must bring it to ~1.3x before the flag is the default.
- Parser rewrites are tempting and expensive; plan 27 looked and found no case for one. Revisit only with a profile.

## Where this is going (rough order)
1. ~~Tooling, type core, first corpus, binder, flow, modules, underlines, corpus to zero, second corpora, plan 19
   R1-R9, suite plans 13-16, 21-24, JSX~~ done.
2. ~~Deferred references (29), substitution types (30), fourth corpus (31), JS stage 2, calls resolve once (28)~~ done.
3. ~~Checkjs corpus (33), JS stage 3, error text tool (34), parser perf, permissive grammar (27), zod corpus~~ done.
4. ~~Mixins (32), silent type differences (35 stages 1-2), parallelism planning (36), perf, lexical diagnostics~~ done.
5. ~~Parallel checking (36 stages 2-3), tsc mechanism ports (37 A-F), webpack and zod-root~~ done.
6. ~~xstate corpus, tsc's inference flow (38 stages 1-2), parallel loading (36 stage 1), the long tail with sub-agents
   (checker bugs, JS, parser, types component)~~ done.
7. ~~Two new corpora (drizzle, vue), perf claw-back, plan-sized hacks, circularity stack (39), resolve once (38
   stage 3), planning declaration diagnostics (40) and project references (41)~~ done.
8. ~~Declaration diagnostics (40), project references (41) with tsc-src and typescript-eslint, laziness planned (42)
   and stages 1-2, memory like-for-like, the types/symbols long tail~~ done.
9. ~~Wider oracles (effect, vscode, the breadth sweep of plan 44), plan 42 stages 3-4, AST memory (43), parallel
   name resolution (45 stage 1)~~ done.
10. ~~Oracles for missing diagnostics (46: flag flips, mutations), `$ExpectType` at breadth (44 stage 3), speculative
   checks (47: not needed), two new corpora (excalidraw, tanstack-router), a perf session~~ done.
11. ~~Retarget to TypeScript 6.0 + stable ordering (plan 48): suite, oracle caches, re-pinned corpora, perf against
   tsgo 7~~ done.
12. ~~Contextual-check state (plan 51), the source-code breadth sweep (plan 52), a hacks audit, plan 50, a perf
   session~~ done.
13. ~~TypeScript 7.1 as the target and tttt as a drop-in `tsc --noEmit` (plans 53, 54): CLI parity, the native suite,
   tsgo-based oracle tools and caches, 7's removals deleted, the 7.x deltas by fan-out~~ done.
14. ~~Perf at breadth (plan 59), memory (plan 60), the bench against tsgo 7.1, plan 55 (pseudochecker), the
   source sweep and suite tails, own regression tests, lib checking~~ done.
15. ~~Plan 61 (constraint functions), plan 62 stage 1 (output independent of K), plan 63 stage 1 (struct diet),
   scaling probes, planning `.d.ts` emit (64) and the last tests (66)~~ done.
16. ~~The suite to 99.9% (plan 66: two fan-outs, content mappers), the checker count following the cores (plan 62),
   planning memory against bun (plan 67), the `.d.ts` writer (plan 64 stages 1-3)~~ done.
17. ~~Checker recycling (67 stage 1), the CLI writing `.d.ts` with maps (64 stages 4-5), the hacks audit (71),
   binder and struct diets, core scaling planned (72), a real-code fan-out, emit scale probes~~ done.
18. ~~The loader's serial work (72 stage 1), plans 73-74, `.d.ts` for JS (64 stage 6), per-file parse arenas (63
   stage 2), the memory endgame (75: no compact AST, a 1.5 GB checker cap), CLI modes measured (76, parked)~~ done.
19. ~~Memory: deferred function bodies (78), mmapped source text (80), the copying recycle measured and declined
   (79), the budget floor, a perf session, the memory bench against bun and tsgo~~ done.
20. ~~Fresh repositories (77: 96.1% on day one), the defaults' memory (81: the budget policy, two density items),
   deferred bodies' accessors, a hacks audit, checker balance by work stealing (72), a perf session~~ done.
21. ~~Debt (the deferred-body leak, plans 82 stage 2, 83, 84), the declaration emit fan-out, a second refresh (emit
   and checkJs), a perf session, message text as an oracle (clidiff)~~ done.
22. ~~The many-core defaults (a CPU column, the checker-count policy, plan 85: no-go), plan 82 stage 3, the clidiff and
   real-code tails by fan-out, a third refresh (`-b` oracle, every tsconfig)~~ done.
23. ~~PGO + LTO (86 stage 1), plan 86's stage 4 gate (go), stage 2's ranking, plan 87 planned and prototyped (go),
   a real-code fan-out~~ done.
24. ~~Plan 86 stage 4's design and gate G0 (go), plan 87 stage 1 (one `-b`), costliest-file-first and the front
   end measured, a real-code fan-out~~ done.
25. ~~Plan 86 stages 4a-4c to gate G1 (plan 88 stage B inside 4a), 4d (`--shared-graph` on threads) to gate G2,
   stage 3's front end, the IntersectionState audit: the first coordinator day~~ done.
26. **Now: plan 86 stage 4e** (variance lock, shared relation cache, the arena split and recycle, the redo's gaps,
   the oracle set with the flag, default on), with the 12-core bench with and without the flag. Then plan 87 stage
   2 and a fourth refresh. Plan 58 (7.1.0 stable, then plan 66 stage 5's fan-out) as soon as it's on npm, ahead of
   everything; plan 76 only if the human reopens it.
