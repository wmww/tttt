# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-09-26, insights after plan 23)
- **Suite 8202 / 13540** fully pass (errors 9980, symbols 11830, types 9989). Split: **TS tests 8045 / 12491 (64%)**,
  JS tests (`@allowJs`/`.js` units) 157 / 1049. Since the insights after plan 13 (5125 / 13163): plans 15 (missing
  checks), 21 (display fidelity), 22 (JSX: 313 of 457 JSX tests pass), 24 (binding patterns), 23 (symbol merging).
  History per plan is in `measurements.md` and git.
- **Corpora** (all clean under tsc 5.8): tsc's `src/compiler` **0**, type-fest **53**, valibot **61**, react
  **1** (bulletproof-react's Vite app, added by plan 25: React 18 + react-query/router, radix, react-hook-form + zod,
  Storybook, vitest; 362 at first run, 80 after plan 25, 3 after plan 19's R10-R14 session: tsc's skip-context-
  sensitive first pass and intra-expression inference, `this` of intersection members, and a few evaluation rules;
  1 after the second plan 19 session). Suite 8258 after that session (TS7053 & co., tuple inference/relation ported
  from tsc, tail-recursive conditionals). What blocks the rest of type-fest is structural: tttt lowers aliases and
  type references eagerly where tsc defers them (plan 19 R18/R19), and has no substitution types (R17).
- **Perf** (`measurements.md`, 2026-09-26; tsgo = typescript 7.0.2 on 4 cores): tsc corpus 0.89 s vs tsgo 1.27
  (single-threaded 1.73), type-fest 0.37 vs 1.96, valibot 0.82 s / 530 MB vs 1.62 s / 822 MB, react 0.31 s / 232 MB
  vs 1.06 s / 545 MB (tsgo single-threaded 0.94). Ahead everywhere, single-threaded; react is parse-dominated
  (1511 dependency files). The tsc corpus has drifted from 0.67 s (after the last perf session) to ~0.8-0.9 s.
- **Built and holding**: hand-written backtracking parser (JSX included), tsc-shaped binder with persistent symbols
  and merging, interned `Ty` core with id-keyed relations and tsc's identity relation, mapper instantiation, two-pass
  inference with priorities, contextual typing, conditional/mapped/indexed/template types, flow graph + narrowing,
  module resolution + tsconfig, iteration protocol, generators, `this` types, elaboration, JSX (namespaces,
  components, runtimes), tsc's type printer and symbol chains for the baselines. ~54k lines of C.
  `documentation.md` describes it, `hacks.md` lists what is approximated.
- **JS/JSDoc (plan 26)**: stage 1 done 2026-09-27 (JSDoc `@type`/`@param`/`@returns`/casts lowered into the
  annotation slots at parse time, CommonJS modules, `require` aliases): JS tests 160 -> 247, suite 8345. Stages 2-4
  (expandos and JS classes, typedef/template declarations, the other tags and TS8xxx) are unscheduled.
- **Suite checker bugs (todo entry 8, 2026-09-27)**: suite 8408 -> 8480 from features that were simply missing
  rather than subtle rules: JSON modules (were any), divergent accessor write types, noUncheckedIndexedAccess (parsed,
  inert), returns of async functions/generators (never checked), indexed access *types* (never checked). Worth a
  sweep for more parsed-but-inert options and TODO'd checks. The two things backed out both need tsc's constraint of
  `T[K]` (triage.md).
- **Absent**: substitution types, union origins, lazy reverse mapped types, decorator checking,
  declaration emit (out of scope: typechecker only, `--noEmit` semantics), project references, parallelism.

## Course correction (insights after plan 23, 2026-09-26)
- **The corpora were left alone for ~30 sessions** while the suite went 6912 -> 8202. The suite work was real
  (symbols, JSX, binding patterns, merging are language features, not test-chasing), but this is exactly the pattern
  the corpus rule exists to catch: nobody was scheduled on them. **Every todo window from now on has corpus
  sessions**, and plan 19 is renamed "corpora to zero" and covers every corpus.
- **A React corpus is the next oracle (plan 25, done: `--corpus react`).** It answers plan 22's open question: the suite's JSX tests
  are hand-written namespaces, real React is `@types/react` overloads, `LibraryManagedAttributes`, HOCs and hooks'
  inference, and it's the benchmark where tsgo is weakest. Its root causes join plan 19.
- **JS/JSDoc starts now: planned (plan 26, `./jsstats.py`), first stage right after.** 892 of the 5338 failing tests (17%)
  are JS, and they are the source of most spurious TS2304/TS2580. It's a real part of what tsc checks
  (`checkJs` projects, mixed code bases), and its binder side (CommonJS exports, expando assignments as
  declarations) is foundational, so it must be designed rather than trickled in.
- **Parser recovery: decided, no rewrite (plan 27).** Stage 1 (the framework) mostly done 2026-09-27: the mask,
  skip-or-abort lists and commit points fit the backtracking parser as predicted, at no measurable parse cost once
  the lookahead stays off valid code's path; 687 -> 583 differing tests, suite +63. 639 TS tests' parse errors differed from tsc's, but only ~1/3
  is recovery; the rest is permissive grammar (tsc parses `with`, modifiers anywhere, odd index signatures and
  heritage clauses, then complains from the checker: ~190) and lexical diagnostics (escapes, numbers, templates,
  unicode identifiers: ~130). A scanner fixes neither. Recovery gets one framework (tsc's parsingContext mask, list
  terminators, skip-or-abort, commit points instead of backtracking past them), then patching. Parser perf is the
  perf session's question, not a rewrite's.
- **The rest of the suite is the type core's long tail.** Missing and spurious TS2322/TS2339 (~200 tests with a
  single-code diff), `./displaydiff.py`'s `other` (378 TS tests): mostly checker bugs that the types baseline shows
  first. Work them like a corpus (sample, reduce, fix the tsc rule), not as display polish. Union origins stay
  deferred (~25 tests, a type-core feature: revisit when a real project's messages need it). Members as symbols: not
  needed for any diagnostic so far; the check-time replays of tsc's member tables are in hacks.md.
- **Process.** Two failure modes, both fixed in `CLAUDE.md`: (1) the insights cadence drifted (10 planned sessions
  became ~15 as plans extended and triage queued new plans ahead of insights); todo.md now has a fixed budget that
  overruns come out of. (2) Four sessions ended without committing, the two with transcripts because they ended their
  turn waiting on a background job; ending the turn ends the session. Otherwise health is good: every plan since
  plan 08 finished without a redesign, nothing lost in the suite.

## Decisions still in force (from earlier course corrections; details in git history of this file)
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
- Perf: deep generic code is where tsc's caches and shortcuts matter; React is measured now (plan 25:
  ahead of tsgo; now that it's checked nearly completely, the perf check-up should re-measure it).
  Checks accumulate cost silently between perf sessions.
- JS/JSDoc is a second front end on top of the checker: the risk is a parallel type world for JSDoc. It must lower
  into the same `Ty` machinery.
- Parser rewrites are tempting and expensive; plan 27 looked and found no case for one. Revisit only with a profile.

## Where this is going (rough order)
1. ~~Tooling, type core, first corpus, binder, flow, modules, underlines, corpus to zero, second corpora, plan 19
   R1-R9, suite plans 13-16, 21-24, JSX~~ done.
2. **Now**: React corpus (25), corpora to zero (19), JS/JSDoc (26), parser recovery (27), the type core's long tail
   on the suite.
3. Then: decorators, substitution types, union origins, `in`/`out` variance annotations, evolving arrays,
   target-gated diagnostics (triage.md "Category-sized").
4. Parallelism and perf tuning against the corpora: parallel parse/bind, sharded checking.
