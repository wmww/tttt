# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-09-28, insights after the window of plans 36-37)
- **Suite 10543 / 13540** fully pass (errors 11983, symbols 12594, types 11613); JS tests 560 / 1049. **+328** in the
  ten sessions since the last insights (10215), all committed, nothing lost. Smaller than the last window's +806 by
  design: two sessions built parallel checking, one planned plan 37, one was perf. Plan 37 landed A-F as prototyped
  (+124, exactly the measured numbers), suite checker bugs 5 (four sub-agents) gave +165, triage +27.
- **Where the 2997 failures are**: types 1927, errors 1557, symbols 946. Errors: 443 missing+spurious, 365 where tttt
  reports nothing, 286 other text, 243 only missing, 116+80 only spurious; 252 have a spurious parser diagnostic.
  Spurious TS2304 (414 in 116 tests) is spread thin, much of it downstream of parser recovery.
- **Corpora** (all clean under tsc 5.8): tsc `src/compiler`, valibot, react, nest, type-fest, zod **0**; webpack
  **9** (from 107; what's left is plan 26 and a PackFileCacheStrategy chain); **xstate 19** (pinned 2026-09-28 as the
  inference-heavy corpus, 394 at pinning, 61 before plan 38 stage 1, 43 after it); **zod-root** reports exactly tsc's 55.
- **Perf**: 4 checkers on up to 4 threads by default, output independent of the thread count. tsc corpus 0.53 s
  (tsgo 1.26), webpack 0.41 (0.67), zod 0.45 (2.92), valibot 0.39 (1.65) on 2 cores x 2 SMT. Variance measurements
  report unreliability like tsc and the `Prop`/`PropDecl` split cut the arena 20-31%; type counts are at or under
  tsc's. Serial parts now: load+parse (117 ms of the tsc corpus) and name resolution (91 ms); the check phase is bounded
  by the heaviest file (one checker 292 ms, the others 90-166).
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, mapper instantiation, two-pass inference with priorities, contextual typing, conditional/mapped/indexed/
  template/substitution types, deferred references, flow graph + narrowing (evolving arrays), module resolution +
  tsconfig, iteration protocol, generators, `this` types, mixins, decorators as calls, the missing type, non-widening
  nullables, union origins, elaboration, JSX, tsc's type printer and symbol chains, JSDoc + CommonJS + expandos,
  parallel checking. ~83k lines of C. `documentation.md` describes it, `hacks.md` lists what is approximated.
- **Absent or approximated tsc mechanisms**: parts of tsc's inference flow (lazy fixing and top-level contextual
  instantiation landed in plan 38 stage 1; a call is still resolved several times, with a replay record standing in
  for tsc's resolve-once; no anyFunctionType first pass), lazy reverse mapped types, permissive instantiation,
  intersection reduction by constraint. Out of scope: declaration emit. Not built: project references (`tsc -b`),
  parallel loading and name resolution.

## Course correction (insights of 2026-09-28, end of the fifth window)
- **Health is very good; no redesign.** Fifth clean window. Plan 37 is the model to repeat: a planning session that
  prototypes every candidate and measures it made the execution sessions predictable (every port landed at its
  prototype's number, 0 lost).
- **Next port: tsc's inference flow (plan 38).** call.c's inference is now the place where approximations are stacked
  (the fixing gate and four shape probes, "stays itself only where it would be unknown", whole-type contextual
  instantiation, the skipped first pass, memoized contextual returns). Five type_core.md bullets and webpack's largest
  cause trace to it, and plan 37 showed the naive gate removal loses tests: the pieces only work as a whole. It is
  riskier than plan 37 (tttt's per-node memoization vs tsc re-checking arguments), so the planning session may
  conclude it needs a per-resolution scope first, or stop. Before it, pin an **inference-heavy corpus** so the port has
  a real-world oracle, not only the suite.
- **Parallel sub-agent sessions stay the main suite engine** (+165 and +210 in the last two). One per window at least,
  plus JS, parser and types-component sessions run the same way.
- **Perf is comfortably ahead** (2-6x tsgo). One perf session per window: now parallel loading (plan 36 stage 1) and
  the check phase's load balance. Parallel name resolution after that, when it is the largest serial part.
- hacks.md is 811 lines again; the triage session prunes it.

## Decisions still in force (from earlier course corrections; details in git history of this file)
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
- **Prefer fixes that move whole categories** (`./failstats.py`, `./displaydiff.py`, the corpora); 13k exact-match
  tests invite special-casing message text.
- **Staging in place works** (the type core migration: 8 sessions, suite runnable throughout, scaffolding deleted).
  The expensive part each time is what the correct rule stops forgiving in the remaining approximations. Don't
  transliterate weak algorithms onto a new foundation; write them the way tsc observably behaves. When a checker
  feature "doesn't work" on lib types, look at what the lib *parsed* to first.
- **A small perf session every few plans**: every check costs a little (plan 15's ~4%). Single-threaded tttt must
  stay ahead of single-threaded tsgo before parallelism is worth building.

## Architecture assessment
A clean, direct-style checker: hand-written backtracking parser straight from `char*` (no tokenizer, fail cache for
the ambiguous cases), an AST of plain structs, a binder (persistent symbols and scopes, merging like tsc's), then
checking driven by `LazyType` (memoised, cycle-detecting per-node types) over the interned `Ty` core. All checker
state is in `Checker`; the AST is read-only while checking.

The five foundational problems the first insights found are all fixed: value-semantic types (-> interned `Ty`),
string-keyed relation caches (-> id-keyed), no control flow analysis (-> plan 05), scopes discarded after resolution
(-> plan 04), checker state in the AST (-> per-`Checker` slots). What's left to watch:
- **Parser**: error-recovery fidelity (plan 27: lists and commit points, no rewrite), unbounded recursion depth (binary chains need ~8 MB of
  stack; tsc walks them iteratively), backtracking cost.
- **Binder**: still writes to shared tables and one arena (`hacks.md`, "Parallelism contract audit"), the one phase
  that doesn't shard per file. JS support will add declarations from assignments; design it with that in mind.
- **Threading**: checking is threaded (stage 3; nothing in the checker had to change once stage 2's audit fixes were
  in, which is evidence the "AST read-only while checking" contract holds). Any new checker-wide memo that *reports*
  must be tagged `Error.once_key`, and anything written to AST/binder state during checking is a race (the frozen
  guards log it; TSan on the corpora is the net: `plans/36_parallelism.md` has the build line). Parsing is
  thread-clean (the prototype's cache-warmer pool is stage 1). The serial resolve-names phase is next after that.

## Risks / concerns
- Exact-match oracle: `types`/`symbols` baselines require tsc's type printing (alias preservation, union order by
  type id, origins, truncation). The interned design can mimic creation order; keep it that way when adding types.
- Perf: deep generic code is where tsc's caches and shortcuts matter (the 2026-09-27 check-up's wins were all tsc
  caches tttt lacked: key-property maps, reverse mapped types, containsType). Checks accumulate cost silently between
  perf sessions (+7% over 09-25..09-27). Wall times drift with the machine: compare callgrind counts across sessions.
- JS/JSDoc is a second front end on top of the checker: the risk is a parallel type world for JSDoc. It must lower
  into the same `Ty` machinery.
- Parser rewrites are tempting and expensive; plan 27 looked and found no case for one. Revisit only with a profile.

## Where this is going (rough order)
1. ~~Tooling, type core, first corpus, binder, flow, modules, underlines, corpus to zero, second corpora, plan 19
   R1-R9, suite plans 13-16, 21-24, JSX~~ done.
2. ~~Deferred references (29), substitution types (30 stages 1-2), fourth corpus (31), JS stage 2, calls resolve once
   (28)~~ done.
3. ~~Checkjs corpus (33), JS stage 3, error text tool (34), substitution fallout (30 stage 3), parser perf, permissive
   grammar (27 stage 2), zod corpus~~ done.
4. ~~Mixins (32), silent type differences (35 stages 1-2), parallelism planning (36), perf (webpack, zod), JS
   leftovers, lexical diagnostics (27 stage 3)~~ done.
5. ~~Parallel checking (36 stages 2-3), tsc mechanism ports (37 A-F), webpack and zod-root~~ done.
6. **Now**: an inference-heavy corpus, tsc's inference flow (38), parallel loading (36 stage 1), the long tail with
   sub-agents (suite checker bugs, JS, parser, types component).
7. Then: plan 37 stage 2 leftovers, parallel name resolution, plan 35/34 queues, project references (real projects
   use `tsc -b`), a token cache / node arenas in the parser.
