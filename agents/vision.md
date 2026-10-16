# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-10-16, insights after the seventeenth window)
- **Target: TypeScript 7.1** (`main` at the 7.1 nightly fed0bf24, `thirdparty/typescript`; re-pin to 7.1.0 stable
  once it is on npm, expected after 2026-11-24: plan 58; on 2026-10-16 `next` was still `7.1.0-dev.20261015.1`). Its
  native suite: 13556 / 13568 (99.9%), the last 12 off the table (argued or one-test mechanisms,
  bugs/tsgo_divergences.md); with own tests and the dts component **13698 / 13740**. Every oracle cache is tsgo
  7.1's, every probe runs tsgo (`test/oracle`: a Go overlay, so a patched tsgo is one `go build` away).
- **Drop-in `tsc --noEmit`, and drop-in `tsc` where the config only emits declarations** (plans 53, 64): tsc's
  output formats, exit codes and flags, `.d.ts` and `.d.ts.map` written (`-b` too). dtsdiff **1077 / 1233** projects
  byte-identical (JS sources are 77 of the 156 left: plan 64 stage 6), `clidiff.py --emit` 309 / 396. JS emit is out
  of scope (the human, 2026-10-12). **Refused**: `--watch`, `--incremental`'s `.tsbuildinfo`, `--showConfig`,
  `--explainFiles`, `--init` (plan 76 decides which a drop-in needs).
- **Real code**: corpora 0 / 0 everywhere but vue (0 / 2), **DT sweep 8595 / 8595**, `$ExpectType` 68639 / 68640,
  flagflip 18 / 6 / 3, scenarios 210 / 0 failed, **source sweep 3352 / 3363**, kdiff 1 / 3363, recycle 4 / 3363.
- **Perf**: tttt is 2-6x faster than single-threaded tsgo and 1.3-3x faster than Bun's `bun check` (a Rust tsgo port)
  at every core and single-threaded (plan 67 "Where it stands"). **Memory is the one axis we lose**: 1.6-2.3x bun's
  at the defaults, up to 1.75x single-threaded (vscode 2.5 GB vs 1.4). Checker recycling (plan 67 stage 1) took
  vscode 4.2 -> 2.6 GB; what remains is the program (the AST: 1.74 GB of vscode's 2.19 GB, 38% zero words) and each
  checker's 128 MB floor plus its own copy of the libraries' types, which grows with the core count. On the
  human's 12-core machine (2026-10-16): vscode 2.50 s vs bun 2.92 vs tsgo 7.95, but 6.5 GB vs 2.1; checkers past 12
  buy 0-11% for ~170 MB each, and a 64 MB recycle budget at K24 saves 1.75 GB for +2.4% (plan 75 question 1).
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, stable type ordering, mapper instantiation, tsc's inference flow (return mappers included) and
  ContextChecked/context anchors, one circularity stack, conditional/mapped/indexed/template/substitution types,
  deferred references, flow graph + narrowing, module resolution + tsconfig, iteration protocol, generators, `this`
  types, mixins, decorators, union origins, elaboration, JSX, tsc's type printer and symbol chains, JSDoc + CommonJS +
  expandos as 7 has them, parallel loading / resolving / checking with the output independent of the checker count,
  checker recycling, declaration diagnostics, the declaration writer with comments and maps, project references /
  `-b`, 7.1's content mappers, tsc's laziness and deferred checks, 7.1's language. ~145k lines of C.
- **Absent or approximated**: pseudo-type builder gaps (plan 55's list), namespace-import clones
  (cloneTypeAsModuleType), permissive instantiation, member identity (members aren't Symbols; a class's member symbols are one
  checker table, interfaces merged with classes still apply rules on top), `.d.ts` for JS sources (plan 64
  stage 6), the CLI modes above.

## Course correction (insights of 2026-10-16, end of the seventeenth window)
- **The window did what it planned, in ten sessions and 114 commits**: memory (recycling: vscode -40% peak at +2-4%
  wall, output byte-identical at any budget), `.d.ts` emit shipped through the CLI with maps (dtsdiff 932 -> 1077),
  the hacks audit (908 -> 573 lines, four artifact hacks deleted rather than documented), core scaling planned with a
  "not now" for parallel declare/link backed by numbers, six quadratic emit paths found by a new probe. The source
  sweep and DT moved with it (3344 -> 3352, 8595 / 8595). 0 lost tests at every merge, no process failures. The
  human paused the loop after entry 10 and restarted it (`claude-stop` is gitignored now: never commit it).
- **Correctness on real code is in the long tail; the project's open questions are now product questions.** What is
  left is (a) memory against bun, (b) scaling past 4 cores, which this box can't measure, (c) what a user replacing
  `tsc` still can't do with tttt (watch, incremental). None of them is test-chasing, and all three decide whether tttt
  is *the faster tsc* or a faster checker for CI only.
- **Memory decision: go for the program's memory, staged with a prototype gate.** Plan 63 stage 2 (per-file parse
  arenas) is done (2026-10-16: the AST in `ast_alloc.h` arenas, any size without malloc's classes; it also showed
  that `MADV_HUGEPAGE` past the system's free 2 MB blocks costs vscode a third of its wall time in compaction stalls:
  the glibc re-exec is gone, arenas take huge pages within a `/proc/buddyinfo` budget and prefault). Then plan 75 *prototypes* the compact AST on the one node family that dominates the zero words and measures
  vscode's heap and the instruction counts before anyone commits to churn under ~2300 AST sites. The prototype may say
  no; if it says yes, the compact AST is the next window's centrepiece. Plan 75 also owns the per-checker floor at
  many cores (12 checkers x 128 MB is 1.5 GB of budget where bun's per-thread state is small): a budget policy that
  doesn't scale with K is cheaper than any AST work, and the shared-lib-layer "no" (plan 67) gets re-asked at K=12.
- **CLI completeness is the other gap (plan 76, planning).** `--watch` and `--incremental` are how most people run
  tsc locally; a 2-6x faster full check may or may not beat tsgo's incremental rebuild on a one-line edit. Planning
  measures that and recommends; building watch/incremental is a scope decision the report raises with the human.
- **Debt is under control, keep paying it**: plans 73 and 74 delete a hack each and are scheduled; every fix still
  gets a `test/cases` repro. Watch the instruction counts: recycling costs type-fest +16% at K1.

## Decisions still in force (from earlier course corrections; details in git history of this file)
- **The suite's last 12 are off the table** (2026-10-15): each is argued (bugs/tsgo_divergences.md) or a plan-sized
  mechanism for one test; work them only if real code shows the mechanism or it falls out of other work. Fidelity to
  tsgo *artifacts* (a runaway's model, a cache collision) is special-casing in a respectable form: don't.
- **The type count is the perf oracle** (2026-10-11): tsgo's `--extendedDiagnostics` `Types`/`Instantiations` are
  machine-independent and directly comparable with tttt's; a ratio far above 1 is a missing cache or a runaway, found
  without timing anything on this shared 4-vCPU box (`scripts/perfsweep.py`).
- **Thread defaults** (2026-10-12, the human asked why 4): `--threads` defaults to every core (parsing and name
  resolution scale with it), and since plan 62 (2026-10-14) `--checkers` follows them: one checker per thread, at
  most one per 64 KB of checked source (a small project's second checker saves milliseconds and copies the lib
  types). tsgo's output is K-independent and so is tttt's now (kdiff: only inbox-zero's undefinedProperties order,
  which tsgo shares). On 12 cores / 24 threads (2026-10-16): K4 -> K12
  1.2-2.0x faster, K12 -> K24 0-11%, at ~170 MB per checker on vscode: the budget or a K cap should follow (plan 75).
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
- **Competition**: bun check (a Rust tsgo port, 2026-10) beats tttt on memory and, on many cores, ties it on wall time
  (it uses every core; tttt did too only from plan 62 stage 3). If tsgo itself gets a similar pass, our margin is single-threaded speed
  (2-6x) minus what core scaling and memory cost us: plans 62 (done) and 67.
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
18. **Now**: the loader's serial work (72 stage 1), plans 73-74, `.d.ts` for JS (64 stage 6), per-file parse arenas
   (63 stage 2), the memory endgame's prototype gate (75), CLI modes (76). Plan 58 (7.1.0 stable re-pin, then plan
   66 stage 5's fan-out) once it's on npm, whenever that falls.
19. Then: the compact AST if plan 75 says yes, what plan 76 and the human decide (watch / incremental), checker
   balance, parallel declare/link only for webpack-like serial declare (12 cores: 5% on vscode, 15% webpack).
