# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-09-28, insights after the window of plans 32 B-36)
- **Suite 10215 / 13540** fully pass (errors 11709, symbols 12530, types 11353); JS tests 519 / 1049. **+806** in the
  ten sessions since the last insights (9409), the best window so far, all committed, nothing lost: plan 32 finished
  (outer type parameters, mixins), plan 35 stages 1-2, a perf session, plan 36 planning, plan 26 stage 3 leftovers,
  plan 27 stage 3 (lexical), a corpus session, suite checker bugs 4 (four parallel sub-agents, +210), triage.
- **Where the 3325 failures are**: types 2187, errors 1831, symbols 1010. Errors: 510 missing+spurious, 400 where
  tttt reports nothing, 297 other text, 252 only missing, 219+132 only spurious, 253 with a parser diff. The
  single-code tests are now mostly *spurious* diagnostics (TS2339 40, TS2322 39, TS2304 35).
- **Corpora** (all clean under tsc 5.8): tsc `src/compiler`, valibot, react, nest, type-fest, zod all **0**; webpack
  **107** (checkJs, JSDoc leftovers). New: **zod-root** (zod's root tsconfig, not clean under tsc: diff it; 28
  spurious in tttt, mostly arktype's string DSL inferring `never`).
- **Perf** (`measurements.md`): checking runs on threads now (plan 36 stage 3: 4 checkers on up to 4 threads by
  default): tsc corpus 0.48 s (tsgo 1.26), webpack 0.40 (0.67), zod 0.45 (2.92), valibot 0.38 (1.65), all on a
  2-core/4-SMT machine. Single-threaded tttt was already ahead of single-threaded tsgo everywhere. Peak RSS grows
  with the checkers (+2..+47%, react worst) but stays at or under multi-threaded tsgo's. Load + name resolution are
  now 40-90% of most runs and serial: plan 36 stage 1 (parallel loading) and parallel name resolution are next.
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, mapper instantiation, two-pass inference with priorities, contextual typing, conditional/mapped/indexed/
  template/substitution types, deferred references, flow graph + narrowing, module resolution + tsconfig, iteration
  protocol, generators, `this` types, mixins, override and variance-annotation checks, elaboration, JSX, tsc's type
  printer and symbol chains, JSDoc + CommonJS + expandos. ~78k lines of C. `documentation.md` describes it,
  `hacks.md` lists what is approximated.
- **Absent or approximated tsc mechanisms** (each a HACK that the long tail keeps hitting, see below): the missing
  type (exactOptionalPropertyTypes), non-widening null/undefined (nullWideningType), evolving arrays (autoArrayType),
  union/keyof origins, decorator checking (decorators as calls, TS1238/TS1241/TS1270), context-free return-only
  signatures for sensitive arguments, lazy reverse mapped types, permissive instantiation, reduction of intersections
  by constraint. Out of scope: declaration emit. Not built: project references, parallelism.

## Course correction (insights of 2026-09-28)
- **Health is very good; no redesign.** Fourth clean window in a row, and the biggest (+806). Parallel sub-agents in
  worktrees (suite checker bugs 4, triage) roughly doubled what one session delivers; keep doing that for
  independent small fixes.
- **The long tail now keeps hitting the same approximations.** Sessions increasingly end with "the fix needs tsc's
  X, which tttt doesn't have": plan 35's top item is nullWideningType ("medium, risky"), the zod fixes left
  `exact_missing_flag()` standing in for the missing type, call.c's fixing gate "stands in for something else",
  evolving arrays are a HACK in three files. Each small session works around the same missing mechanism a little
  more. This is the "almost works" pattern: **port the mechanisms instead** (plan 37). One planning session ranks
  them by suite + corpus impact and risk, derisks the top ones with prototypes and writes a sub-plan each; the
  window then executes the top two. Same shape as the type core ports of plans 29-30, which paid.
- **Parallel checking is built (plan 36 stages 2-3)**: a fixed logical checker count (`--checkers`, default 4,
  files dealt round robin; the harness runs 1), any thread count gives byte-identical output (`./threadcheck.sh`),
  TSan clean on every corpus. K=4 differs from K=1 only in union member order inside messages. Next: parallel loading
  (stage 1, now a bigger share since checking shrank) and parallel name resolution (binder per-file arenas). Memory
  grows with K: the `Prop` split (`perf_leads.md`) is the counterweight if needed.
- Corpora: webpack's 107 and zod-root's 28 have named causes; one corpus session.

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
5. **Now**: parallel checking (36 stages 2-3), tsc mechanism ports (37: planning, then the top two), webpack and
   zod-root, the spurious-diagnostic long tail.
6. Then: the rest of plan 37 (decorators, union origins, ...), parallel loading (36 stage 1), memory (`Prop` split,
   type counts), plan 35/34 queues, a token cache / node arenas in the parser, parallel name resolution.
