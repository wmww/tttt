# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-09-27, insights after plans 26-30 were written)
- **Suite 8579 / 13540** fully pass (errors 10388, symbols 12044, types 10320); JS tests ~250 / 1049. +377 in the ten
  sessions since the last insights (8202): plan 26 stage 1 (JSDoc core + CommonJS), plan 27 stage 1 (parser list
  recovery), two corpus sessions, a suite checker bugs session, a perf check-up, a triage. Nothing lost on the way.
  Of the 4961 failing tests, 1488 fail errors only by *missing* diagnostics, spread over hundreds of codes (the top,
  TS2339/TS2322, are in <90 tests each): small missing checks, module/target-gated diagnostics, JS. Spurious errors
  concentrate in the parser (472 tests have a spurious TS1xxx; most of the 924 spurious TS2304 follow bad recovery).
- **Corpora** (all clean under tsc 5.8): tsc's `src/compiler` **0**, valibot **0**, react **0** (bulletproof-react's
  Vite app, plan 25: 362 at first run), nest **0** (brocoders/nestjs-boilerplate: NestJS 11, TypeORM, Mongoose,
  aws-sdk, decorators, CommonJS; plan 31: 134 at first run, 0 the same day), type-fest **19** (all with known causes:
  plan 30 stage 3, plan 32 mixins, R20, the exactOptional missing type). The 2026-09-27 corpus session took valibot
  49 -> 0 and react 1 -> 0 with ~30 tsc rules; since then expect-type's `toEqualTypeOf` mismatches are visible
  (they were silently passing), so valibot's test-d files now check tttt's identity relation too.
  **Four of five corpora are at 0: the next insights session should pick a fifth** (plan 19's lesson: a new corpus
  finds tens of spurious diagnostics in its first session).
- **Perf** (`measurements.md`, 2026-09-27 perf check-up; tsgo = typescript 7.0.2 on 4 cores): tsc corpus 0.87 s vs
  tsgo 1.27 (single-threaded 1.77), type-fest 0.48 vs 1.93, valibot 0.81 s / 541 MB vs 1.64 s / 827 MB, react 0.30 s /
  242 MB vs 1.06 s / 553 MB (tsgo single-threaded 0.96). Ahead everywhere, single-threaded. The "drift" to 0.93 s was
  mostly the machine (~15% slower than on 09-25); callgrind counts are the stable measure: 6.92 G -> 5.91 G that
  session (tsc's key-property map, reverse mapped cache, containsType). Calls now resolve once (plan 28, -6%: 6.17 G ->
  5.79 G). Next lever: the backtracking parser (~20% of instructions). nest: 0.30 s vs tsgo 0.40 s (parallel), the closest margin
  yet: with skipLibCheck an application is mostly parsing (170 ms of 300), so the parser matters there. valibot's
  plan 31 regression is fixed (eager instantiation of mapped instances; 0.81 s, 5.64 G). 2026-09-27 check-up: tsc
  corpus 0.90 s vs tsgo 1.54 s on a slower day, every corpus 2-5x ahead of parallel tsgo except nest (0.31 vs 0.58).
- **Built and holding**: hand-written backtracking parser (JSX included), tsc-shaped binder with persistent symbols
  and merging, interned `Ty` core with id-keyed relations and tsc's identity relation, mapper instantiation, two-pass
  inference with priorities, contextual typing, conditional/mapped/indexed/template types, flow graph + narrowing,
  module resolution + tsconfig, iteration protocol, generators, `this` types, elaboration, JSX (namespaces,
  components, runtimes), tsc's type printer and symbol chains for the baselines, JSDoc + CommonJS. ~64k lines of C.
  `documentation.md` describes it, `hacks.md` lists what is approximated.
- **JS/JSDoc (plan 26)**: stage 1 done 2026-09-27 (JSDoc `@type`/`@param`/`@returns`/casts lowered into the
  annotation slots at parse time, CommonJS modules, `require` aliases): JS tests 160 -> 247, suite 8345. Stages 2-4
  (expandos and JS classes, typedef/template declarations, the other tags and TS8xxx) are next; stage 2 is scheduled.
- **Absent**: union origins, lazy reverse mapped types, decorator checking,
  declaration emit (out of scope: typechecker only, `--noEmit` semantics), project references, parallelism.

## Course correction (insights of 2026-09-27)
- **Health is good; no redesign needed anywhere.** Every plan since plan 08 finished without one; the last window ran
  exactly its ten sessions and committed each. The planning sessions now prototype before writing (plans 29 and 30
  come with measured prototypes): keep doing that, it's why execution sessions stopped overrunning.
- **The corpora stalled for five sessions (type-fest 53, valibot 60-61)**, but for a known reason: what's left is two
  type-core ports (plan 29: lazy mapped templates and tsc's deferred interface references, R18/R19; plan 30: tsc's
  indexed-access simplification and constraints, then substitution types, R17). Both are ports of named tsc
  functions, not new architecture. They go first in this window, back to back.
- **The corpora are running out of signal.** tsc 0 and react 1 no longer find bugs; type-fest/valibot are
  type-level libraries. Nothing checks server-side code (`@types/node`, ORMs, DI containers, decorators, CommonJS
  output settings). **Plan 31 added a fourth corpus** (NestJS; 134 -> 0 in one session, see plan 19). Lesson: a
  clean corpus only finds *spurious* diagnostics, so decorator-heavy code that tttt doesn't check at all looks
  perfect; missing checks (decorators included) are found by the suite only. The rule stands: a rising pass count
  with flat corpus counts is test-chasing.
- **The suite's remaining errors are a long tail, not a category.** Work it in "suite checker bugs" sessions driven by
  failstats' single-code buckets (the last two such sessions moved +72 and +98) and the module/target-gated
  diagnostics (TS1202/1203/2441/2725/1262..., triage.md "Target-gated"). Plan 27 stage 2 (permissive grammar, ~190
  tests, error-only code shapes) waits until after this window: real code rarely has syntax errors.
- **JS stage 2 (expandos, JS classes) is scheduled**: it's the binder side (assignment declarations) that vision
  flagged as foundational; stages 3-4 later.
- **hacks.md is 914 lines.** Still useful, but the triage at the end of this window should prune entries fixed by
  plans 26-30 and consolidate, the way triage.md was pruned.

## Decisions still in force (from earlier course corrections; details in git history of this file)
- **Every todo window has corpus sessions** (the corpora were once left alone for ~30 sessions), and todo.md has a
  fixed session budget that overruns come out of; ending a turn ends the session, so commit before long jobs.
- **Parser: no rewrite** (plan 27 looked; recovery is one list/commit-point framework plus patching). Revisit only
  with a profile. **JS must lower into the same `Ty` machinery**, no parallel JSDoc type world.
- **Real projects are the second oracle.** A rising pass count with flat corpus counts means we're test-chasing.
  Rank corpus diagnostics by root cause, not by code (one mapped type with an `as` clause was 810 TS7006). Every
  insights session looks at the corpus numbers next to the pass count. Corpus root causes that are type-core rules go
  ahead of suite plans.
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
2. **Now**: deferred references (29), indexed-access constraints and substitution types (30), a fourth corpus (31),
   corpora to zero (19), JS/JSDoc (26 stage 2), the suite's long tail, resolve calls once (28).
3. Then: JS stages 3-4, parser permissive grammar (27 stage 2), decorators (as the fourth corpus asks), union
   origins, `in`/`out` variance annotations, evolving arrays.
4. Parallelism and perf tuning against the corpora: parallel parse/bind, sharded checking.
