# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal.

## State (2026-09-22, plan 17 done)
- **Two modern corpora (plan 17)**, `thirdparty/ensure-corpora.sh`, `./realworld.py --corpus type-fest|valibot`, both
  clean under tsc 5.8: type-fest **598** diagnostics, valibot **4473**. Ranked causes: module augmentation of an
  aliased export (vitest's `expect`, ~2400 of valibot's: plan 19 stage 0), template literal types / string mappings /
  `as` clauses (plan 18: type-fest's arithmetic utilities, expect-type's messages), then a long tail (plan 19 loop).
- **valibot is the first corpus where tttt is slower than tsgo**: 4.0 s / 1.7 GB vs 1.3 s / 0.8 GB, 93% instantiating
  conditional types during overload inference with no instantiation cache (plan 20). tsc corpus and type-fest are
  still well ahead (0.74 vs 1.02 s; 0.28 vs 1.37 s, though type-fest's check is incomplete).
- Suite 4219. Plan 17 also added `@ts-expect-error`/`@ts-ignore` (+TS2578) and bigint literal types.
- **Real corpus: `./realworld.py`** (tsc's own `src/compiler` via its tsconfig, 223 files incl. `@types/node`, clean
  under tsc): **0 diagnostics** (1751 at the start of plan 08; plans 09-12 took the category-sized buckets, the last
  plan 08 session the 31 singles: ~20 root causes, each a tsc rule tttt didn't have, see the git log).
- **The first honest perf comparison** (`measurements.md`): with every check done, tttt checks the corpus in **0.88 s
  vs tsgo 1.02 s** (tsgo single-threaded 1.72 s, tsc 6.36 s), RSS 370 vs 425 MB. Single-threaded tttt is ~2x
  single-threaded tsgo. Finishing the checks that used to fail early cost next to nothing. Typecheck is 567 ms of the
  880, and its profile is narrowing -> relations (`ty_is_related` 4.3 M calls) and union building.
- **Suite: `./fasttest.sh` 4211 / 13163 fully pass**. Plan 07 (types baseline underlines) made the types component
  a real measurement of type *printing*; plans 08-12 moved the suite by +715 on the side.
- Plan 12 gave literals tsc's fresh/regular twins and object literals freshness + excess property checks (TS2353),
  done in one session. It exposed the next missing piece of error *reporting*: tsc's elaborateError (errors located
  at the offending property/element of a literal, plan 16 stub).
- Plan 11 removed the last big approximation in the generic core: relations instantiate a generic source in the
  context of the target instead of unifying positionally / erasing to `any`, and instantiated generic signatures get
  fresh type parameters (bounded ones) instead of sharing them with their target.
- **Suite gaps are now mostly absent features, not wrong answers** (triage after plan 10): ~1250 tests hit a syntax
  error tsc doesn't have (`using` alone ~330: plan 14); dozens of checks don't exist at all (`./unimplemented_codes.sh`:
  unused locals, property vs index signature, used-before-declaration, operand checks, grammar/regex checks: plan 15);
  no accessibility/readonly/private-name checks (plan 13), no error elaboration (plan 16 stub). These are
  cheap per test compared with the type core work, and ready to schedule now that the corpus is done.
- The last plan 08 session added tsc rules the corpus needed: getQuickTypeOfExpression (declaration cycles through
  loops), auto types (`let x;` typed by control flow; evolving arrays still `any[]`), symbol-keyed element access
  references, removeOptionalityFromDeclaredType, inferFromAnnotatedParameters, hasContextualTypeWithNoGenericTypes,
  members of generic homomorphic mapped types, getEffectiveConstraintOfIntersection, typeRelatedToDiscriminatedType.
  One new hack on an old one: the eager discriminant-conflict reduction of intersections now skips properties it would
  have to lower mid-cycle (tsc reduces lazily, `hacks.md`); that area produced the only order-dependent result seen.
- `this` types (plan 10: polymorphic `this`, contextual `this`, class heritage checks) and the iteration protocol +
  unique symbols + generators (plan 09) landed, both done in one long session each without a redesign.
- ~31k LoC of C in `src/`. Done and holding: parser (hand-written, backtracking, no tokenizer; parses the whole
  corpus), binder (tsc-shaped symbols, persistent scopes, aliases, lib overlay: plan 04), the `Ty` type core
  (interned, id-keyed relations, mapper instantiation, two-pass inference, contextual typing, conditional/mapped/
  indexed types: 8 sessions), flow graph + narrowing (plan 05), module resolution + tsconfig (plan 06), compiler
  options in one struct, multi-config tests, JS gate (`allowJs`/`checkJs`), diagnostics without a file.
  `documentation.md` describes the architecture, `hacks.md` what is approximated in each part.
- Absent: JS/JSDoc semantics (~900 tests now fail quietly behind the gate), JSX, template literal types, mapped `as` clauses, substitution types, reverse mapped types,
  decorators checking, declaration emit (out of scope). The iteration protocol, unique symbols and generators landed
  in plan 09 (`ty/iterate.c`).
- History: every foundation plan (type core, binder, flow, modules) moved the suite by a whole category in one go and
  made it faster. Per-plan numbers are in `measurements.md` and the git log.

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
- Perf: ahead of tsgo on tsc's compiler (0.74 vs 1.02 s), **behind on valibot** (4.0 vs 1.3 s, conditional type
  instantiation during overload inference: plan 20). Deep generic code is where tsc's caches matter, and tttt skipped
  some of them.
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
4c. ~~Second real corpus (plan 17)~~ done (type-fest + valibot). Now the type-core features they need (plan 18:
   template literal types, key remapping), valibot's perf (plan 20), the corpora to zero (plan 19). Suite plans 13-16
   alongside.
5. Broad feature fill driven by the aggregation tool: classes, overloads, inference, mapped/conditional/template
   types, enums, namespaces, decorators, JSX, JS/JSDoc.
6. Parallelism and perf tuning against the benchmark. Single-threaded tttt must beat single-threaded tsgo first.
