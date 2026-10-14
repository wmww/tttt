# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-10-14, insights after the fifteenth window)
- **Target: TypeScript 7.1** (`main` at the 7.1 nightly fed0bf24, `thirdparty/typescript`; re-pin to 7.1.0 stable
  after 2026-11-24: plan 58). Its native suite: **13455 / 13568** (99.2%), plus 65 own regression tests (all passing) (`test/cases`,
  every fix found outside the suite gets one). Every oracle cache is tsgo 7.1's, every probe runs tsgo (`test/oracle`:
  a Go overlay on the pinned checkout, so a patched tsgo is one `go build` away).
- **Drop-in `tsc --noEmit`** (plan 53): tsc's output formats, sorting, exit codes and flags; `scripts/clidiff.py`
  compares stdout byte for byte. The human (2026-10-12): JS emit is out of scope (esbuild and co. cover it); .d.ts
  emit is our call: plan 64 says build it (TS sources first, ~5 sessions), making tttt a drop-in for
  `tsc --emitDeclarationOnly` too.
- **Corpora** 0 spurious everywhere but effect (3); missing: webpack 1, vue 3. **DT sweep** 8594 / 8595,
  **`$ExpectType`** 68637 / 68640, **flagflip** 42 / 6 / 3, **scenarios** 210 passed / 0 failed (14 skipped),
  **source sweep** 3332 / 3363, **kdiff** (1 vs 16 checkers) 4 / 3363 projects differ.
- **Perf**: single-threaded tttt is 2.0-5.8x faster than single-threaded tsgo 7.1 on the corpora, 2.5-9.6x at both
  defaults, at 0.15-0.6x its RSS (measurements.md "Perf session (2026-10-14)"). **At breadth** (`scripts/perfsweep.py`,
  1786 source sweep projects): check time median 0.16x tsgo's, types 0.68x, worst types ratio 1.62, no project above
  tsgo's peak RSS. **At scale** (`scripts/scaleprobe.py`): no single declaration, block or expression shape is
  superlinear in tttt where tsgo is linear (five quadratic paths fixed 2026-10-14). **Bun's `bun check`** (a Rust port of tsgo 7.0.2): tttt is 1.1-2.7x faster at equal threads,
  but bun uses every core and 2-3x less memory (vscode at defaults on 12 cores: bun 3.3 s / 2.1 GB, tttt 4.4 s / 6.0
  GB, tttt at 12 checkers 3.1 s / 7.6 GB). Plan 60 took vscode's peak -16%; the rest is representation.
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, stable type ordering, mapper instantiation, tsc's inference flow and ContextChecked/context anchors,
  one circularity stack, conditional/mapped/indexed/template/substitution types, deferred references, flow graph +
  narrowing, module resolution + tsconfig, iteration protocol, generators, `this` types, mixins, decorators, union
  origins, elaboration, JSX, tsc's type printer and symbol chains, JSDoc + CommonJS + expandos as 7 has them,
  parallel loading / resolving / checking, declaration diagnostics, project references / `-b`, 7.1's content mappers
  (a real JSON-RPC host; the suite runs tsgo's Go test mappers as a process), tsc's laziness and
  deferred checks, 7.1's language (source phase imports, ambient-module attributes, es2026). ~129k lines of C.
- **Absent or approximated**: pseudo-type builder gaps (plan 55's list), namespace-import clones (cloneTypeAsModuleType), permissive instantiation,
  member identity (members aren't Symbols; decided acceptable), `.d.ts` emit (plan 64, planned, not started).

## Course correction (insights of 2026-10-14, end of the fifteenth window)
- **The window did what it planned, and planned the rest.** Plan 61 finished the last core-rule approximation
  (tsgo's constraint functions, circularity included); plan 62 stage 1 made the output checker-count independent
  everywhere but 4 source sweep projects; a fan-out took the suite 13464 -> 13515 and the source sweep 3310 -> 3332
  with 0 lost; scaling probes found and fixed five quadratic paths no corpus showed. Plans 63 (AST memory), 64 (`.d.ts`
  emit, verdict yes) and 66 (the last ~113 tests) were written. hacks.md 771 -> 804 lines, mostly notes, not debt.
- **The miss is memory.** Plan 63's struct diet got vscode's single-threaded peak 4.40 -> 4.16 GB against a 4.0
  target, and what's left of it (stage 1b) is ~-90 MB of increments. Bun is at 2.1 GB at defaults on the same
  project. More diets won't close a 2x gap: the next window spends one *planning* session (plan 67) measuring where
  the peak is (AST, binder, checker, per-checker duplication) and costing the structural options (32-bit node
  references in per-file arenas, a shared lib type layer) before any more memory code.
- **The human asked for 100% of the suite.** Plan 66 is the route: two fan-outs, content mappers (plan 56, 10
  tests, reversing its "not now"), and the emit-order tests that ride on plan 64's writer, because tsgo's harness
  emits before it collects diagnostics. The no-special-casing rule holds: a test that can't pass honestly gets a
  written argument in tsgo_divergences.md and a line in report.md, not a hack.
- **Two process failures this window, both now rules (CLAUDE.md Workflow):** the disk filled (a 50 GB Go build cache
  from instrumented tsgo builds; now capped in `ensure-typescript.sh`), and the triage session ended with four
  finished commits stranded in a worktree (merged by this insights session, 0 lost). Sessions end with an empty
  `git worktree list` and their /tmp scratch deleted.
- **Order this window:** the suite's fan-out and content mappers first (the human's request), the checker count
  following the cores (plan 62 stages 2-3, the cheaper half of the bun gap), the memory plan, then plan 64 to a
  working TS-source writer and the second fan-out. Plan 64's later stages (comments, CLI, declarationMap) and plan
  67's execution are the next window's.

## Decisions still in force (from earlier course corrections; details in git history of this file)
- **The type count is the perf oracle** (2026-10-11): tsgo's `--extendedDiagnostics` `Types`/`Instantiations` are
  machine-independent and directly comparable with tttt's; a ratio far above 1 is a missing cache or a runaway, found
  without timing anything on this shared 4-vCPU box (`scripts/perfsweep.py`).
- **Thread defaults** (2026-10-12, the human asked why 4): `--threads` defaults to every core (parsing and name
  resolution scale with it), and since plan 62 (2026-10-14) `--checkers` follows them: one checker per thread, at
  most one per 64 KB of checked source (a small project's second checker saves milliseconds and copies the lib
  types). tsgo's output is K-independent and so is tttt's now (kdiff: only inbox-zero's undefinedProperties order,
  which tsgo shares, and mermaid's relation order). K 4 -> 16 costs +7..47% RSS; wall-time scaling past 4 cores
  waits on the human's 12-core bench (triage.md).
- **Fan-out merge discipline** (2026-10-13): full source sweep match-set diffs and flagflip against a session-start
  run at every fan-out merge (they caught 11 lost projects and a -31 TS6196 nothing else saw).
- **Fan-out sessions** (four Opus sub-agents in worktrees, one failstats/displaydiff/jsstats bucket each, disjoint
  test lists, 0 lost enforced at merge) are the engine for the long tail: one or two per window. **Instruction counts
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
- **Competition**: bun check (a Rust tsgo port, 2026-10) beats tttt on memory and, on many cores, ties it on wall time
  (it uses every core; tttt did too only from plan 62 stage 3). If tsgo itself gets a similar pass, our margin is single-threaded speed
  (2-6x) minus what core scaling and memory cost us: plans 62 and 63.
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
16. **Now**: the suite to 100% (plan 66: fan-outs, content mappers 56), the checker count following the cores (plan
   62 stages 2-3), planning memory against bun (plan 67), `.d.ts` emit to a working writer (plan 64 stages 1-2).
   Plan 58 (7.1.0 stable re-pin) once it's on npm (after 2026-11-24).
17. Then: plan 64 stages 3-5, plan 67's execution, parallel declare/link (bugs/perf_leads.md "Parallelism"), a token
   cache in the parser.
