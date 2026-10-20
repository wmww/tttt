# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-10-20, insights after the twenty-third window)
- **Target: TypeScript 7.1** (`main` at the 7.1 nightly fed0bf24, `thirdparty/typescript`; re-pin to 7.1.0 stable
  once it is on npm, expected after 2026-11-24: plan 58; on 2026-10-20 npm's `latest` was 7.0.2, `next`
  7.1.0-dev.20261019.1). Its native suite: 13556 / 13568 (99.9%), the last 12 off the table (argued or one-test
  mechanisms, bugs/tsgo_divergences.md); with own tests and the dts component **13944 / 13978** (dts 1885 / 1907).
  Every oracle cache is tsgo 7.1's, every probe runs tsgo (`test/oracle`: a Go overlay, so a patched tsgo is one
  `go build` away).
- **Drop-in `tsc --noEmit`, and drop-in `tsc` where the config only emits declarations**, TypeScript and JavaScript
  sources alike (plans 53, 64). **clidiff** (the whole `--pretty false` output, byte for byte, every corpus and
  source sweep project): **3824 / 3831**. dtsdiff **1329 / 1350** projects (part of the rest is tsgo's own checker
  split). JS emit is out of scope (the human, 2026-10-12). **Refused**: `--watch`, `--incremental`'s
  `.tsbuildinfo`, `--showConfig`, `--explainFiles`, `--init` (plan 76, parked by the human 2026-10-16; asked twice
  whether to reopen it, no answer: stays parked).
- **Real code**: corpora 0 / 0 everywhere but vue (0 / 2, plan-sized: tsc's in-flight-free member resolution), **DT
  sweep 8595 / 8595**, `$ExpectType` 68640 / 68640, flagflip 18 / 3 / 2, scenarios 210 / 0 failed, **source sweep
  5234 / 5242 projects** (every `tsconfig*.json` since 2026-10-19; three refreshes, 96.1%, 97.1%, 98.1% on day one),
  **`-b` sweep 402 / 416 solutions** (406 with plan 87's store), kdiff 2, recycle 4.
- **Perf and memory** (12-core box, PGO + LTO shipped: measurements.md "The 12-core box with PGO + LTO" and "bun
  canary"): tttt is fastest single-threaded everywhere (1.1-2.9x bun canary, vscode 10.4 s against tsc-rs's 14.0)
  and uses the least CPU at the defaults but tsc-rs's 4 checkers on 3 / 14. **At the defaults bun canary
  (oven-sh/bun#45800, 2026-10-19) beats tttt's wall on 4 / 14** (vscode 2.00 against 2.44 s, drizzle, excalidraw,
  nest) at 1.7x tttt's CPU, and bun's peak is lower on 10-12 / 14. tttt's K12 spends 2.3-4.4x its own K1 CPU:
  clocks 1.62x, duplicated instantiations ~1.2x (drizzle 1.4x), recycling 5-8%. Bun's output isn't tsc's (drizzle
  21 errors against 41).
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, stable type ordering, mapper instantiation, tsc's inference flow (return mappers included) and
  ContextChecked/context anchors, one circularity stack, conditional/mapped/indexed/template/substitution types,
  deferred references, flow graph + narrowing, module resolution + tsconfig with tsgo's two-phase file collection,
  iteration protocol, generators, `this` types, mixins, decorators, union origins, elaboration, JSX, tsc's type
  printer and symbol chains, JSDoc + CommonJS + expandos as 7 has them, parallel loading / resolving / checking with
  the output independent of the checker count, checker recycling, deferred function bodies, per-file AST arenas,
  declaration diagnostics, the declaration writer with comments and maps for TypeScript and JavaScript, project
  references / `-b`, 7.1's content mappers, tsc's laziness and deferred checks, 7.1's language. ~150k lines of C.
- **Absent or approximated**: pseudo-type builder gaps (plan 55's list), namespace-import clones
  (cloneTypeAsModuleType), member identity (members aren't Symbols), the CLI modes above.

## Course correction (insights of 2026-10-20, end of the twenty-third window)
- **The perf window shipped its cheap stages and found the next one is the big one.** PGO + LTO took 11-17%
  instructions (-8-9% wall) on every corpus, the untrained ones too. Plan 86's stage 4 gate said go: id-decided
  union ties are 0.003-0.6% and unobservable on every oracle once the spread-of-a-union class has a structural key.
  Stage 2's first session took ~2% from a flat profile. Plan 87 (`-b` reads emitted declarations) said go and was
  faster and more correct in one prototype session. The cut-short window still ran a real-code fan-out (source
  sweep 5225 -> 5234) and a triage.
- **Decision: plan 86 stage 4 (one type graph shared by every checker) goes next, ahead of stage 2** (plan 86
  "Order"). The human asked for multiples on many cores sooner. bun canary now wins on wall at the defaults on 4 /
  14 by keeping ~20 threads busy, while tttt's extra checkers each rebuild the same instantiations: K24 costs
  40-160% more CPU for 0-7% wall. A shared graph is the only lever that makes more checkers pay and that cuts memory
  at the defaults K-fold, our other weak axis. It's the riskiest change since the type core (every lazily written
  field becomes publish-once or scratch), so it gets a design session with a census and a prototype with a go/no-go
  before any build-out window. Its fallback, if no, is stage 3 (front end) plus the snapshot recycle for the
  thrashers. The rule "faster than bun on every corpus at the defaults" stays the target; it is currently broken,
  and this is the plan to restore it, not a reason to tune the defaults' policy again (five policies were already
  declined along one frontier).
- **Single-threaded speed is the multiplier, but its cheap part is spent.** With hardware counters unavailable here,
  stage 2's remaining levers (instruction footprint, intern table layout) can't be confirmed except by wall time.
  Keep them for perf slots and measure them on wall at K1.
- **Two `-b` models must not outlive the next window**: plan 87 stage 1 deletes the sources model.
- **Correctness is in its long tail and holds** (5234 / 5242 projects, clidiff 3824 / 3831, every corpus clean but
  vue). Each window keeps one real-code session; a fourth refresh comes after the stage 4 gate.

## Course correction (insights of 2026-10-19, end of the twenty-second window) (superseded where the 2026-10-20 one disagrees)
- **The window did what it said, and the correctness side is in its long tail.** Plan 82 finished, clidiff 3612 ->
  3823 / 3831 (text mismatches 54 -> 1), dtsdiff 1244 -> 1325 / 1350, a third refresh at 98.1% on day one, the `-p`
  sweep widened to every `tsconfig*.json` (3814 -> 5242 projects, 99.7%) and a new `-b` oracle (400 / 416). Each
  refresh's day-one rate has risen (96.1, 97.1, 98.1): the checker generalizes. Correctness work now comes from
  refreshes and new oracle axes, not from the existing oracles' tails; fan-outs on a 99.7% sweep are getting smaller.
- **Perf is where the multiples are, and the order is set (plan 86, the human's ask).** Plan 85 closed no-go
  (declared-world duplication is 2-6% of the extra CPU); the many-core accounting says clocks 1.62x, duplication
  ~1.2x, recycling 5-8%. The next window is perf-heavy: PGO + LTO shipped, the stage 4 gate measured early (it
  decides whether a shared type graph is even possible under our determinism rule), and stage 2's memory-system
  sessions. CPU at the defaults is reported but still not a bound.
- **`-b` reads its references' emitted declarations: go (plan 87, prototyped 2026-10-19).** The sources model needed
  a new approximation per divergence (builder window, untyped private members, stripped `@internal` members) and
  couldn't express errors inside emitted `.d.ts`, body-only imports or tsgo's emit leaking downstream. The prototype
  (an in-memory store of each program's declarations, read by downstream programs through a per-program `Fs` view,
  `-p`'s outputs mode) gains 6 `-b` sweep projects without losing one, holds the scenarios, and is *faster* (tsc-src
  wall -23%, CPU -31%, RSS -32%): checking against declarations more than pays for the ordering constraint. Stage 1
  deletes the sources model; stage 2 shares parsed declaration files across programs like tsgo's build host.
- **Robustness of a drop-in**: a source truncated mid-run no longer SIGBUSes (fixed 2026-10-20: `text_sigbus()`
  zeroes the page and warns once).

## Course correction (insights of 2026-10-18, condensed 2026-10-20)
- Debt windows after feature or memory windows are cheap (one session each, as sized): keep scheduling them.
- "Saturated" oracles hid a whole axis (54 text mismatches under 0 / 0 corpora): every oracle that compares a
  projection should say what it doesn't compare.
- tsc-rs (a Rust port of tsgo at our pin, tsgo-exact) is tracked next to bun in report.md: its edge is CPU
  efficiency at few checkers.
- Plan 85 (a shared *declared* layer) closed no-go: declaration files' declarations are 2-6% of the extra CPU; what
  is rebuilt is instantiations of the project's own code. That is why plan 86 stage 4 shares the whole graph.
- Price many-core work by per-file time sums under real parallel runs, never by serialized CPU.

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
- **Thread defaults** (2026-10-12, the human asked why 4): `--threads` defaults to every core (parsing and name
  resolution scale with it), and since plan 62 (2026-10-14) `--checkers` follows them: one checker per thread, at
  most one per 64 KB of checked source (a small project's second checker saves milliseconds and copies the lib
  types). tsgo's output is K-independent and so is tttt's now (kdiff: only inbox-zero's undefinedProperties order,
  which tsgo shares). On 12 cores / 24 threads (2026-10-16): K4 -> K12
  1.2-2.0x faster, K12 -> K24 0-11%, at ~170 MB per checker on vscode: the CLI caps the checkers' budget at 1.5 GB together (plan 75).
  **Since 2026-10-18 the default checkers stop at the physical cores** (but every thread up to 4: on 2 cores / 4
  threads K2 is 10-26% slower than K4, vscode then level with bun): 24 threads oversubscribed on 4 vCPUs spend 6-26%
  more CPU at K24 than K12 on the corpora, 31-42% on the thrashers (dokploy, monkeytype) and 0.2-2 GB more peak
  (measurements.md "Checkers per physical core"). Unconfirmed on the 12-core box; the human's command is there. The
  budgets stay (larger ones from the start trade along plan 81's frontier: vscode K12 -10% CPU, +1.2 GB).
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
- **Binder**: declare and link are serial and write shared tables; the resolve walk shards per file, JavaScript too
  (plan 45: a JS file's assignment declarations replay in program order; merged type parameters still walk serially;
  `hacks.md`, "Parallelism contract"). JS support will add declarations from assignments; design it with that in mind.
- **Threading**: checking is threaded (stage 3; nothing in the checker had to change once stage 2's audit fixes were
  in, which is evidence the "AST read-only while checking" contract holds). Any new checker-wide memo that *reports*
  must be tagged `Error.once_key`, and anything written to AST/binder state during checking is a race (the frozen
  guards log it; TSan on the corpora is the net: documentation.md "Typecheck" has the build line). Loading is
  threaded too (a cache-warmer pool beside the unchanged sequential loader: `src/load_pool.c`; the shared state is the
  resolver's locked caches). Name resolution shards per file (plan 45 stages 1-2); declare/link are serial.

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
  least CPU at its defaults and is tsgo-exact. tttt is fastest on wall everywhere (12-core: 0.26x tsgo's wall against
  bun's 0.41x) and single-threaded by 2x+; our margin is single-threaded efficiency, which the defaults currently
  spend on SMT inflation, recycling and (at many checkers) duplicated instantiations (plan 85's census).
- **Determinism vs scaling**: the output must stay byte-identical at any checker and thread count, so across machines
  (`scripts/kdiff.py`, `scripts/checkerdiff.py`, `scripts/threadcheck.sh`; `scripts/oracles.py` runs kdiff).
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
24. **Now: plan 86 stage 4** (a shared concurrent type graph: design, prototype, gate, then its own build-out
   window), plan 87 stage 1 (one `-b`), costliest-file-first and the front end measured (stage 3), one real-code
   session. Then the build-out or the fallback (stage 3 + the snapshot recycle), plan 87 stage 2, a fourth refresh.
   Plan 58 (7.1.0 stable, then plan 66 stage 5's fan-out) as soon as it's on npm, ahead of everything; plan 76 only if
   the human reopens it.
