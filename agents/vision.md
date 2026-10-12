# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-10-11, insights after the thirteenth window)
- **Target: TypeScript 7.1** (`main` at the 7.1 nightly fed0bf24, `thirdparty/typescript`; re-pin to 7.1.0 stable
  after 2026-11-24: plan 58). Its native suite is the suite: **13316 / 13571** (98.1%), 0 lost all window. Every
  oracle cache is tsgo 7.1's, every probe runs tsgo (`test/oracle`: a Go overlay on the pinned checkout, so a patched
  tsgo is one `go build` away).
- **Drop-in `tsc --noEmit`** (plan 53): tsc's output formats, sorting, exit codes and flags; `scripts/clidiff.py`
  compares stdout byte for byte. Check-only by design (readme.md). The human (answered 2026-10-12): JS emit is out of
  scope (esbuild and co. cover it); .d.ts emit is our call, unclear importance. Not before the checker and perf goals.
- **Corpora** 0 spurious everywhere; missing: webpack 5, drizzle 3, vue 3, effect 3 spurious. **DT sweep** 8592 /
  8595, **`$ExpectType`** 68631 / 68640, **flagflip** 132 / 16 / 6, **scenarios** 209 / 213, **source sweep** 3119 /
  3363 (88 / 174 repos).
- **Perf**: on the curated corpora, single-threaded tttt is ~5x faster than single-threaded tsgo 7.1 with less
  memory (type-fest: 1.45 s / 2.0 GB vs 8.0 s / 3.3 GB). **At breadth** (plan 59, done 2026-10-12: 1779 source
  sweep projects tsgo checks, `scripts/perfsweep.py`): tttt makes 0.68x tsgo's types at the median, no project above
  1.55x (was 16.4x), and no project above tsgo's peak RSS. The fan-out's ~15 fixes were all tsgo caches and lazinesses
  tttt lacked (instantiations keyed by the whole mapper, eager work tsgo defers); they also cut drizzle -17% and xstate
  -9% instructions. **Bun 1.4.3's `bun check`** (a Rust port of tsgo 7.0.2, 2026-10-11) ties tttt's default wall
  time on a 12-core box and uses less memory on 10 of 14 corpora (vscode 2.1 GB vs our 6.0 GB): memory and core
  scaling are now the competitive gap, not single-threaded speed.
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, stable type ordering, mapper instantiation, tsc's inference flow and ContextChecked/context anchors,
  one circularity stack, conditional/mapped/indexed/template/substitution types, deferred references, flow graph +
  narrowing, module resolution + tsconfig, iteration protocol, generators, `this` types, mixins, decorators, union
  origins, elaboration, JSX, tsc's type printer and symbol chains, JSDoc + CommonJS + expandos as 7 has them,
  parallel loading / resolving / checking, declaration diagnostics, project references / `-b`, tsc's laziness and
  deferred checks, 7.1's language (source phase imports, ambient-module attributes, es2026). ~129k lines of C.
- **Absent or approximated**: pseudo types (`src/ty/pseudo.c`, tsgo's pseudochecker: the types baseline, messages
  and isolatedDeclarations all go through them; plan 55 has the builder gaps left), content mappers (plan 56, deliberately not done), namespace-import clones (cloneTypeAsModuleType), permissive
  instantiation, member identity (members aren't Symbols; decided acceptable).

## Course correction (insights of 2026-10-11, end of the thirteenth window)
- **The 7.1 switch was a clean success.** Ten sessions did plan 53 and plan 54: CLI parity, the native suite,
  Go-based oracle tools, every cache regenerated, what 7 removed deleted, and three fan-outs that took the suite from
  87% at the switch to 98.1%. JS shrank (-2246 lines: 7 removed constructor functions and friends) and hacks.md fell
  798 -> 709 lines. The predicted dip-and-recover took two sessions, not a window. No course correction needed on
  correctness: the tail is now plans 55/56 and per-project source sweep bugs.
- **The window's blind spot was perf, and it's the project's actual goal.** Three things changed under us: the
  oracle is now tsgo 7.1 (faster than 7.0.2, never re-benchmarked, `bench.sh` still not re-baselined), a second
  competitor appeared (bun check, which already wins on memory), and the source sweep showed that our "3-14x" comes
  from corpora we have tuned for months. **This window is perf-first**: perf at breadth (plan 59: the gate fix, a
  tsgo-vs-tttt perf sweep ranked by type-count ratio, a fan-out on the outliers), then memory (plan 60, to be
  planned: bytes per type and per node against bun's bar), then the re-baselined bench. Correctness keeps two fan-outs
  (source sweep, suite/JS tail) and plan 55.
- **The type count is the perf oracle we were missing.** Wall times on a shared 4-vCPU box are noisy (the sweep's
  "tttt as slow as tsc" line was measured while four agents ran). tsgo's `--extendedDiagnostics` `Types` and
  `Instantiations` are machine-independent and directly comparable with tttt's (type-fest 4.78M vs 5.10M), so a
  ratio far above 1 is a missing cache or a runaway, found without timing anything.
- **Thread defaults** (decided 2026-10-12, the human asked why 4): `--threads` defaults to every core (parsing and
  name resolution scale with it; checking uses at most K threads), `--checkers` stays a fixed 4. K is part of the
  output: which checker makes a type decides union order in messages, so a core-dependent K would make output
  machine-dependent, and each checker duplicates lib types (memory). Raising the fixed K, or making checking scale
  past K without changing output, needs numbers from a many-core machine (agents can't measure past 4 vCPUs here).

## Decisions still in force (from earlier course corrections; details in git history of this file)
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
- **Competition**: bun check (a Rust tsgo port, 2026-10) already beats tttt on memory. If tsgo itself gets a
  similar memory or perf pass, our margin is the corpora's 3-14x minus whatever perf at breadth reveals. Memory per
  type/node is the lever we've worked least on since plan 43.
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
14. **Now**: perf at breadth (plan 59), memory against bun's bar (plan 60, to be planned), the bench re-baselined
   against tsgo 7.1; plan 55 (pseudochecker); the source sweep tail. Plan 58 (7.1.0 stable re-pin) after 2026-11-24.
15. Then: parallel declare/link (bugs/perf_leads.md "Parallelism"), a token cache / node arenas in the parser, thread
   defaults that scale with cores (with numbers from a many-core machine).
