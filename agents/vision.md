# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-09-27, second insights of the day, after the window of plans 29-31)
- **Suite 8952 / 13540** fully pass (errors 10748, symbols 12176, types 10498); JS tests 299 / 1049. +373 in the ten
  sessions since the last insights (8579), all ten committed, nothing lost: plans 29 and 30 (deferred references,
  indexed-access simplification, substitution types), plan 31 (fourth corpus), plan 26 stage 2 (expandos, JS
  classes), a corpus session, suite checker bugs (+245, the biggest single session yet), perf (plan 28), triage.
  Of the 4588 failing tests: 1177 fail errors only by missing diagnostics, 420 only by spurious ones, 736 both,
  **431 have the right codes at the right places and the wrong text** (plan 34), 476 have a spurious TS1xxx (plan 27).
- **Corpora** (all clean under tsc 5.8): tsc's `src/compiler` **0**, valibot **0** (from 60), react **0**, nest **0**
  (NestJS app, 134 at first run, 0 the same day), type-fest **11** (known causes: plan 32 mixins 4,
  R20 6, exactOptional 1). expect-type's `toEqualTypeOf` mismatches are visible now, so valibot's test-d files check
  tttt's identity relation too.
- **Perf** (`measurements.md`; tsgo 7.0.2 on 4 cores): tsc corpus 0.90 s vs tsgo 1.54 s (1 thread 1.90), type-fest
  0.43 vs 2.35, valibot 0.81 / 467 MB vs 2.27 / 833 MB, react 0.34 vs 1.32, nest 0.31 vs 0.58. Ahead everywhere,
  single-threaded; nest is the closest (an application with skipLibCheck is mostly parsing: 170 of 300 ms). Callgrind
  on the tsc corpus 6.17 G -> 5.79 G (calls resolve once); valibot's plan 31 regression found and fixed (8.15 -> 5.64 G).
- **Built and holding**: hand-written backtracking parser (JSX included), tsc-shaped binder with persistent symbols
  and merging, interned `Ty` core with id-keyed relations and tsc's identity relation, mapper instantiation, two-pass
  inference with priorities, contextual typing, conditional/mapped/indexed/template types, flow graph + narrowing,
  module resolution + tsconfig, iteration protocol, generators, `this` types, elaboration, JSX (namespaces,
  components, runtimes), tsc's type printer and symbol chains for the baselines, JSDoc + CommonJS. ~64k lines of C.
  `documentation.md` describes it, `hacks.md` lists what is approximated.
- **JS/JSDoc (plan 26)**: stages 1-2 done (JSDoc lowered into the annotation slots at parse time, CommonJS,
  expandos, JS classes and constructor functions): JS tests 160 -> 299. Stages 3-4 (typedef/template, the other tags,
  TS8xxx) are next, ordered by the webpack corpus (checkJs, pinned by plan 33: 3981 diagnostics, essentially all
  unresolved `@typedef`s and ignored `@type` on assignments; plan 26 stage 3 has the order).
- **Absent**: outer type parameters (a local class/interface/alias in a generic function doesn't capture its `T`:
  `f(3).x` is `T`; plan 32 stage B), union origins, lazy reverse mapped types, decorator checking,
  declaration emit (out of scope: typechecker only, `--noEmit` semantics), project references, parallelism.

## Course correction (second insights of 2026-09-27)
- **Health is good; no redesign needed.** The last window ran exactly its ten sessions, every one committed, both
  type-core ports (plans 29, 30) landed as planned from measured prototypes. Keep prototyping in planning sessions.
- **The corpora have done their job and are out of signal**: four of five at 0, type-fest's 19 all have named causes.
  Two gaps: **no corpus exercises JS/JSDoc** (plan 26's next stages would be ranked by test counts alone), and valibot
  is the only inference-heavy library. **Plan 33 pinned webpack** (checkJs; done, its ranking is in plan 26 stage
  3). An inference-heavy application library (tRPC / zod v4 / drizzle) follows.
- **A new, unowned bucket: 431 tests fail only on error text** (same codes, same positions): elaboration chains
  (~180, which relation step reports), type printing in messages (~130: `typeof import("m")`, quoted numeric names,
  qualified namespace names), underlines (~60), related info (~40). No tool bucketed it, so no session worked it.
  **Plan 34**: build the tool, then fix by mechanism. Same lesson as displaydiff/syntaxstats: a bucket gets worked
  once a tool names its mechanisms.
- **Parser is now the top item for both perf and spurious errors.** Perf: ~20% of instructions on the tsc corpus,
  more than half of nest's time (the closest margin to tsgo). Fidelity: 476 tests with a spurious TS1xxx. Plan 27's
  verdict (no scanner, no rewrite) stands for fidelity; this window has a parser perf session (first-char dispatch
  in the backtracking entry points) and plan 27 stage 2. If the perf session finds the backtracking structure itself
  is the cost (not fixable by dispatch), that's the profile vision asked for before revisiting the no-rewrite rule:
  write it up for the next insights rather than starting a rewrite.
- **Suite checker bugs sessions work** (+72, +98, +245): one per window stays, fed by `bugs/missing_checks.md`.
- **Doc sprawl**: plan 19 (443 lines) and measurements.md (551) are mostly history; the triage at the end of this
  window shrinks them. Plan 29 is done (its optional stage 4 moved to `bugs/type_core.md`).

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
- **Threading** (not built, not precluded): N checkers over one AST is "make N `Checker`s and partition files". Type
  ids are per checker, and printed union order depends on them: keep output deterministic under any partition.

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
3. **Now**: a checkJs corpus (33) then JS stage 3, error text fidelity (34), substitution fallout (30 stage 3), mixins
   (32), parser perf and permissive grammar (27 stage 2), the suite's long tail.
4. Then: JS stage 4, an inference-heavy corpus, decorator checking, union origins, `in`/`out` variance annotations,
   evolving arrays.
5. Parallelism and perf tuning against the corpora: parallel parse/bind, sharded checking.
