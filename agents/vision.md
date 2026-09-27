# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-09-27, third insights of the day, after the window of plans 32-34)
- **Suite 9409 / 13540** fully pass (errors 11168, symbols 12349, types 10743); JS tests 395 / 1049. +457 in the ten
  sessions since the last insights (8952), all ten committed, nothing lost: webpack pinned (plan 33), plan 30
  finished, plan 34 stages 1-2 (error text: 432 -> 311), plan 32 stage A, plan 26 stage 3 (JS 310 -> 375), suite
  checker bugs, parser perf, plan 27 stage 2 (parser-diff tests 544 -> 402), zod pinned, triage.
- **Where the 4131 failures are**: the **types component fails most (2797 tests)**, errors 2372, symbols 1191. Of the
  errors: 656 tests where tttt reports nothing, 584 missing+spurious, 389 only missing, 311 other text, 402 with a
  parser diff. Of the tests that pass errors and fail types, most are *not* display: `displaydiff.py`'s unnamed
  `other` (429 tests fixed alone) and `we-any` (166) are wrong types no diagnostic shows (plan 35).
- **Corpora** (all clean under tsc 5.8): tsc `src/compiler` **0**, valibot **0**, react **0**, nest **0**, type-fest
  **4** (mixins, plan 32 C), webpack **135** (checkJs; JSDoc leftovers, plan 26), zod **15** (plan 19: the missing
  type, ProtoOf inference, keyof deferral).
- **Perf** (`measurements.md`; tsgo 7.0.2 on 4 cores): tsc corpus 0.80 s vs 1.24 s, type-fest 0.41 vs 1.94, valibot
  0.74 vs 1.61, react 0.29 vs 1.04, nest 0.26 vs 0.40, zod 1.43 vs 2.95, **webpack 0.69 vs 0.61 (tsgo single-threaded
  0.60): the first corpus tttt loses**. Callgrind on the tsc corpus 5.79 -> 5.05 G (parser dispatch). zod is the
  heaviest (11 G; inference walks fluent classes member by member where tsc infers by type arguments).
- **Built and holding**: hand-written backtracking parser (JSX included), tsc-shaped binder with persistent symbols
  and merging, interned `Ty` core with id-keyed relations and tsc's identity relation, mapper instantiation, two-pass
  inference with priorities, contextual typing, conditional/mapped/indexed/template/substitution types, deferred
  references, flow graph + narrowing, module resolution + tsconfig, iteration protocol, generators, `this` types,
  elaboration, JSX, tsc's type printer and symbol chains, JSDoc (typedefs, templates, imports) + CommonJS + expandos.
  ~66k lines of C. `documentation.md` describes it, `hacks.md` lists what is approximated.
- **Absent**: outer type parameters (a local class/interface/alias in a generic function doesn't capture its `T`;
  plan 32 B), mixin classes (32 C), union origins, lazy reverse mapped types, decorator checking, `in`/`out` variance
  annotations, regex literal checks (TS15xx), JSDoc stage 4 (the other tags, TS8xxx), declaration emit (out of scope),
  project references, parallelism.

## Course correction (third insights of 2026-09-27)
- **Health is good; no redesign needed.** Third clean window in a row (ten sessions, all committed, +373 then +457).
  The process works; the long tail is long (4131 tests) and each window moves ~3.5% of the suite.
- **The types component is the biggest unowned bucket.** 2797 failing tests, and in the ones that pass errors the
  largest kinds are unnamed (`other`) or `we-any`: wrong inference results, wrong literal widening, missing outer
  type parameters, where no error shows the difference. These are checker semantics bugs of the same kind that later
  surface as spurious errors in real code, so they're worth more than their test count. **Plan 35**: tool first
  (split the bucket by mechanism), then fix from the top. Same pattern as plans 27/34.
- **Parallelism gets a planning session now (plan 36)**, not "later". webpack is lost even to single-threaded tsgo,
  nest is within 35% of multi-threaded tsgo, and every window of completeness work costs instructions. Meanwhile the
  binder keeps growing shared state (JS declarations). Parallel parsing is the likely first stage (13-43% of the time
  on every corpus); the planning session measures a prototype before staging anything. The rule "single-threaded
  must stay ahead of single-threaded tsgo" still holds too: this window also has a perf session aimed at webpack
  (JSDoc checking) and zod (inference by type arguments), plus the free wins (`DEBUG_ALLOCATOR` is on in release).
- **Parser**: the perf session's verdict is that no function stands out any more; what's left is structural (a
  `Parser` copy per attempt, identifiers rescanned per alternative, one malloc per node). That points at a token
  cache and node arenas, not a rewrite; the no-rewrite rule stands. Fidelity: stage 3 (lexical, including tsc 5.5's
  regex checks, which tttt lacks entirely) is next.
- **Corpus sessions keep paying**: zod pinned at 87 went to 15 the same day. The remaining corpus counts have named
  causes (plan 32 C, plan 26 leftovers, plan 19 zod items); one corpus session this window.

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
3. ~~Checkjs corpus (33), JS stage 3, error text tool (34), substitution fallout (30 stage 3), parser perf, permissive
   grammar (27 stage 2), zod corpus~~ done.
4. **Now**: outer type parameters and mixins (32 B/C), silent type differences (35), parallelism planning (36),
   perf (webpack, zod), JS leftovers and stage 4, lexical diagnostics (27 stage 3), error text queue (34).
5. Then: parallel parse (36's first stage), decorator checking, union origins, variance annotations, evolving arrays,
   a token cache / node arenas in the parser.
6. Sharded checking, perf tuning against the corpora.
