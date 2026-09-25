# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal.

## State (2026-09-24, insights after plan 13)
- **Suite 6267 / 13163** fully pass (errors 9002, symbols 10403, types 8389; 5125 at the insights after plan 13: +307
  from plan 15 session 1, +545 from plan 21's first execution session, +251 from plan 15 session 2). **Corpora**: tsc's `src/compiler` 0
  diagnostics, type-fest 173, valibot 328 (all three are clean under tsc; 450 / 1871 before plan 19 R5/R3/R2/R4/R1).
  **Perf** (`measurements.md`): tsc corpus 0.70 s vs tsgo 0.75 (single-threaded 1.29), type-fest 0.30 vs 1.29,
  valibot 0.57 s / 413 MB vs tsgo 0.82 s / 823 MB (2026-09-24, after the signature relation fixes): tttt now beats
  multi-threaded tsgo on every real corpus, single-threaded.
- Since the last insights (plan 08), eight plans each finished in one session without a redesign: 17 (two modern
  corpora), 19 stage 0 (module augmentation), 14 (syntax gaps), 18 (template literal types, `as` clauses), 20
  (conditional instantiation cache, valibot 4.4 -> 1.2 s), 13 (member modifiers, `super`), 16 (elaboration, related
  info). Suite 4211 -> 5116 over them.
- Done and holding: hand-written backtracking parser, tsc-shaped binder, interned `Ty` core with id-keyed relations,
  mapper instantiation, two-pass inference, contextual typing, conditional/mapped/indexed/template types, flow graph +
  narrowing, module resolution + tsconfig, iteration protocol, generators, `this` types, elaboration. ~42k lines of C.
  `documentation.md` describes it, `hacks.md` lists what is approximated.
- Absent: JSX (and the suite doesn't even run `.tsx` tests, see below), JS/JSDoc semantics, substitution types, lazy
  reverse mapped types, decorator checking, declaration emit (out of scope).

## Course correction (insights after plan 13, 2026-09-24)
Where the failing suite tests are (`./failstats.py` + a one-off diff of the dumps against tsc's baselines):
- **Missing checks, ~2350 non-JS tests** fail errors with *only missing* diagnostics (tttt says less than tsc, nothing
  wrong). Top codes: TS6133 (unused), TS2339, TS2322, TS2300, TS2411, TS2403, TS2343, TS2695, TS2391, TS2430 (on since
  2026-09-24, with TS2320), TS2872, TS2314, TS2448. That's plan 15, which has
  been "ready" for three insights sessions: it now gets two sessions.
- **Display fidelity, ~2600 tests** pass their errors but fail types and/or symbols (1230 types only, 490 symbols only,
  1060 both); 960 of them are 1-2 lines off, **274 differ only in `^^^` underlines** (plan 07 left 73). By mechanism
  (lines): tsc reprinting a written annotation (`typeof a`, `Array<Base>`: ~290 tests), class expression constructors
  printed as `{ new (): ; prototype: ; }`, symbols reached through an alias/namespace printed as `import("x").A`
  instead of `Widgets.A` (~80 tests), `T_1` renaming (~90 tests), union member order (~90 tests), `any` where tsc has a
  type (~400 tests, mixed causes). No single mechanism is huge, but together this is the biggest pool in the suite, and
  it's also what users read in error messages. **Plan 21 (display fidelity) gets a planning session**, which ranks the
  mechanisms properly and folds in triage.md's "Display fidelity" and "Class expressions and member symbols".
- **`.tsx` tests aren't in the suite at all** (`baseline_test_suite.c` only collects `*.ts`: 328 test files). The pass
  rate never counted JSX, and JSX is part of the TypeScript language (every React code base). First bring them into
  the suite so the number is honest (it will drop), then a JSX plan (22).
- **The modern corpora are the type-core oracle**, and plan 19's root causes are core rules, not polish: an identity
  relation (R1), `const` type parameters (R4), `infer` variables deferring nested conditionals (R5), `keyof` of object
  literal types (R3), mapped-type optionality over unions (R2). Those go first, ahead of the suite plans.
- **Relations under assumptions: done (2026-09-24), and it wasn't a caching bug.** tttt's maybe-stack already was
  tsc's. What differed was *which* inner comparisons tsc runs with reportErrors (overloads elaborate the first source
  signature) and tsc's callback-parameter rule, which also makes `Promise<T>`-like types covariant instead of
  bivariant. That strictness exposed a cluster of latent inference bugs the bivariance had hidden (unawaited async
  returns, no `NonInferrableType`, no target erasure in inferFromSignatures, type parameter capture in
  `clone_type_params`, union generator contexts), all fixed; the erasure alone halved valibot's typecheck time. Lesson:
  a relation that is too lenient hides inference bugs, so expect more of these whenever relations get stricter.
- **JS/JSDoc stays deferred** (~1050 tests, 930 failing, most of the spurious TS2304). Deliberate: it's a second
  language front end on top of the checker; JSX and the TS checks come first. The next insights decides when.
- `hacks.md` has gone partly stale (still says "no nominal class rules", "no `as` clauses" after plans 13 and 18); the
  next triage audits it against the code.
- Process health is good: eight plans in eight sessions, no redesigns, no losses. The risk is the other way: the easy
  suite plans are running out, and what's left is bigger (display, JSX, JS). Don't let the pace push those into
  piecemeal fixes; plan them.

## Course correction (insights after plan 08, 2026-09-22)
- **The first corpus has told us what it can.** It is tsc's own code: ES5-era style, classes and unions, almost no
  type-level programming. The type core has no **template literal types** (`` `a${B}` ``, `Uppercase<>` & co., infer
  from templates), no mapped type **`as` clauses** (key remapping), no **substitution types**, no reverse mapped types.
  The suite underweights those too (~80 tests), but every modern library and a lot of app code leans on them. Zero
  diagnostics on one old-style project is not "the language works". **Plan 17: a second real corpus of modern,
  type-heavy TypeScript** (pick, pin, wire into `realworld.py`/`bench.sh`, measure, rank root causes), next session.
  Expect it to spawn a type-core plan (template literals + key remapping + substitution types); that's type-core
  work, which goes before the suite plans when the corpus asks for it.
- **Then the suite plans, cheapest-per-test first.** 1238 tests have a spurious parse error that kills all three
  components (plan 14: `using` alone ~330). Plan 13 (member modifiers), plan 16 (elaboration: needs planning; 101
  tests differ only in a column and much of TS2322 missing/spurious is misplacement), plan 15 (a loop). Unplanned and
  deliberately later: JS/JSDoc (~900 tests: big, and not "the TypeScript language"), JSX (needs its own plan after
  the second corpus says how much real code needs it), decorators.
- **No perf plan yet.** tttt is ahead of multi-threaded tsgo on one corpus while single-threaded; the profile's top
  (narrowing -> relations, union building) hasn't changed since plan 09 but hasn't grown either. Revisit with the
  second corpus's profile: if that one is slower than tsgo, *that* is the perf plan.
- Process health: plans 09-12 each finished in one session, within budget, no redesigns. The hack list grows mostly in
  "Type core: known gaps", which is now the feature list for the plan 17 follow-up.

## Course correction (insights, 2026-09-22)
- **The corpus is stalling while the suite rises.** 1750 / 1752 / 1751 diagnostics across plan 05, plan 06 and the
  triage. That's the pattern the previous course correction (below) warned about. The suite work was not wasted
  (module resolution was needed), but the corpus is now the next thing: plan 08 works it down root cause by root
  cause, with the suite as a regression check, and ends with the first bench/profile where tttt does all of the
  work tsgo does. Until then the perf headline stays "no evidence of a deficit", not "faster". *(Done: corpus at 0,
  tttt 0.88 s vs tsgo 1.02 s.)*
- **Plan 07 (underlines)** done in one session (1971 -> 3496 full), within budget: the reuse rule is fitted to
  probes, not a port of tsc's node builder (`hacks.md`).
- The spread of the corpus count is smaller than it looks: rank buckets by root cause, not by code (one mapped type
  with an `as` clause = 810 TS7006). The same goes for `failstats.py`.

## Course correction (insights, after the type core migration)
- **tttt had never parsed a real program** until plan 03 (see State). The test suite barely contains regex literals,
  `?.()`, braceless `if/else`, comma operators, so `failstats.py` ranked them as a flat tail and we walked past them
  for 15 sessions while the perf headline came from a synthetic corpus of tests tttt already passes. **Second oracle
  from now on: `./realworld.py` must reach zero diagnostics on a real project, and the benchmark is that project**
  (`./bench.sh`, second table). Every insights session should look at its numbers next to the pass count; a rising
  pass count with a flat real-world count means we're test-chasing.
- The audit vision.md asked for: `hacks.md` is honest and detailed (it reads as a gap list of the new core, which is
  what it should be), the migration did not drift, and the "AST-side walk" section is an acceptable starting point
  for the symbols plan. No redesign of `Ty` is called for.
- **Symbols before flow analysis, as two plans.** The old todo had one planning session for both. The binder
  (`scope.c`, `declaration.c`, ports: ~1.5k lines) is the last legacy foundation: declarations are *copied* for
  imports/aliases/lib sharing (so identity needs `canonical_declaration`, and a copied interface is a second `Ty`),
  same-name namespaces don't merge (177 tests, `Intl`), scopes are thrown away, name resolution is as slow as parsing
  and mutates shared state (blocks parallel bind). Narrowing keys its caches on reference identity -> symbol identity,
  so it can't be built on copies. Plan 04 = symbols/binder (replace, don't patch; it's small), plan 05 = flow graph +
  narrowing on top of it.
- JS files (~900 tests parsed as TS) have the cheap gate (skip without `allowJs`, no semantic errors without
  `checkJs`, done); real JS/JSDoc support waits until TS itself works on real code.

## Architecture assessment
What exists is a clean, small, direct-style checker: hand-written backtracking parser straight from `char*`
(no tokenizer, fail cache for the ambiguous cases), an AST of plain structs, a binder (symbols, persistent scopes: declare, link, resolve), then typechecking driven by `LazyType` (memoised, cycle-detecting per-node types).

Things I believe are fine: C, cleat, the phase structure, LazyType as the memo/cycle mechanism, the parser
approach (fast in principle; watch error-recovery fidelity since tsc's parse errors are part of the oracle).

Things that were or are **foundationally wrong for the stated goal**. 1, 2 and 5 are fixed (the type core
migration, plan 02, now deleted: see git history for the plan and `documentation.md` for the result); 3 and 4 are
fixed too (symbols = plan 04, flow = plan 05, see the course correction above):
1. ~~Types are value-semantic trees with no identity (clone/drop, clone-and-substitute generics)~~ **Fixed**: `Ty`.
2. ~~The relation cache is keyed by a string rendering of both types~~ **Fixed by plan 02 stage 3**: relations run
   on `Ty` with id-keyed caches; `type_pair_cache.h` and the `hash_key` display mode are deleted.
3. ~~No control flow analysis at all~~ **Fixed by plan 05** (4 stages, plan deleted: `documentation.md` "Control
   flow" describes the result, `hacks.md` "Narrowing" what is left out): tsc's flow graph built in the resolve walk,
   `getFlowTypeOfReference` with loops/closures/type facts, type predicates end to end, assertions, `never` calls,
   typed reachability, discriminants, switch, `instanceof`, `in`, TS2454. Real corpus 7777 -> 1626 diagnostics. Stage 4
   took TS2339 / "possibly undefined" out from behind its legacy gate (corpus 1750 now: the +124 are real bugs the gate
   hid, mostly aliased conditions and weak inference, see `triage.md`) and profiled: three algorithmic blowups (none
   in the flow engine itself) took the corpus typecheck from 778 to **352 ms, wall 0.57 s vs tsgo 0.79 s**.
4. ~~Scopes are discarded after name resolution; there's no persistent symbol table~~ **Fixed by plan 04 stage 1**
   (class/interface members are still not symbols, see the plan).
5. **Checker state is stored in the AST** (`LazyType` memos inside nodes), and the test harness shares one lib AST
   between all test programs. Blocks arena-owned types and multiple checkers over one AST. **Fixed by plan 02 stage
   2** (memos in per-`Checker` slots); the `has_cache` fields of legacy type operators went in 4b2.

**Lessons from the type core migration worth keeping** (8 sessions, staged in place with the suite runnable
throughout, scaffolding = a `TYPE_NEW` tag + one-layer legacy views, all deleted again):
- Staging in place worked: flips of one kind of thing at a time were cheap, and the scaffolding never cost more than it
  saved. The expensive part each time was what the new, correct rule *stopped forgiving* in the remaining
  approximations (weak inference, no contextual typing, no narrowing). Expect the same when narrowing lands.
- Don't transliterate weak algorithms onto a new foundation; write them the way tsc observably behaves.
- When a checker feature "doesn't work" on lib types, look at what the lib *parsed* to first (`new (): T`).
- Threading (not built, not precluded): all checker state is in `Checker`, the AST is read-only while checking, so N
  checkers over one AST is "make N `Checker`s and partition files". Type ids are per checker (printed union order
  could depend on the partition; keep it deterministic). Parse is per file. Bind still writes to shared tables and one arena: `hacks.md`, "Parallelism contract audit".

## Risks / concerns
- Test-chasing: 11k tests invite special-casing message text. Prefer fixes that move whole categories.
- Exact-match oracle: `types`/`symbols` baselines require tsc's type *printing* (alias preservation, union
  ordering by type id, truncation). Union order by creation id is something an interned design can mimic and a
  tree design cannot. Keep this in mind when designing ids.
- Perf: ahead of tsgo on tsc's compiler (0.70 vs 0.75 s) and type-fest (0.59 vs 1.30 s), behind multi-threaded tsgo
  on valibot (1.19 vs 0.88 s; was 4.4 s before plan 20's conditional instantiation cache). Deep generic code is where
  tsc's caches and shortcuts matter, and tttt still lacks some (same-alias inference/relation by variance).
- Parser robustness: error recovery paths are barely exercised under valgrind (plan 03's heap corruption turned out
  to be the error *printer*, `span_line_start` on a span starting at a newline, not the parser).
- Single-threaded design. tsgo's win is largely parallel parse/bind + multiple checkers. Arena/interning design
  must not preclude per-file parallel parse/bind and ideally sharded checking (the type core doesn't, see above;
  plan 04 must cover bind). On the real corpus typecheck is 567 of 880 ms, load+parse 192, resolve 102: all three
  shard naturally per file, bind is the one with shared tables.
- JS files, JSX, JSDoc, project references, declaration emit: all absent (tsconfig and module resolution exist
  since plan 06). Declaration/JS emit is out of scope for now (typechecker only, `--noEmit` semantics).

## Where this is going (rough order)
1. ~~Measurement tooling~~ done. Parser gaps (cheap, large, independent of the type core) slot in alongside 2.
2. ~~Type core redesign~~ done (8 sessions).
3. ~~Real-world corpus parses and runs end to end~~ done (plan 03); it is the pinned benchmark and second oracle.
3b. ~~Persistent symbols / new binder (plan 04)~~ done, then bind-time flow graph + narrowing (plan 05).
4. ~~Harness fidelity (compiler options, multi-config tests)~~ done; ~~module resolution~~ done (plan 06); ~~JS gate,
   diagnostics without a file~~ done (triage). ~~Types baseline underlines (plan 07)~~ done (+1525 full passes).
4b. ~~Real corpus to zero diagnostics (plan 08), then bench + profile it~~ done (with plans 09-12): 0 diagnostics,
   0.88 s vs tsgo 1.02 s.
4c. ~~Second real corpus (plan 17)~~ done (type-fest + valibot), plans 18/20 done. Now plan 19's core rules (R1-R5),
   then the corpora to zero. ~~Suite plans 13, 14, 16~~ done; 15 (missing checks), 21 (display fidelity), 22 (JSX)
   next.
5. Broad feature fill driven by the aggregation tool: classes, overloads, inference, mapped/conditional/template
   types, enums, namespaces, decorators, JSX, JS/JSDoc.
6. Parallelism and perf tuning against the benchmark. Single-threaded tttt must beat single-threaded tsgo first.
