# Plan 08: the real corpus to zero diagnostics

## Why
`./realworld.py` (tsc's `src/compiler` through its own tsconfig, clean under tsc) is the second oracle and the only
honest benchmark. It has been flat at ~1750 diagnostics for three plans (05: 1750, 06: 1752, triage: 1751), while
the pass count rose. Every remaining diagnostic is a tttt checker bug, on code people actually write. And the perf
comparison with tsgo (0.6-0.7 s vs 0.72 s) is meaningless until tttt does all the work tsgo does: a failed call
stops checking early. Zero diagnostics is the point where the benchmark becomes a result.

The distribution (2026-09-22) is much less spread than it looks:
- **TS7006 852, of which 810 in one file** (`visitorPublic.ts: visitEachChildTable: VisitEachChildTable = { [SyntaxKind.X]:
  function (node, visitor, ...) {...} }`, `VisitEachChildTable = { [TNode in HasChildren as TNode["kind"]]:
  VisitEachChildFunction<TNode> }`). A small hand-written copy of that shape (`const enum`, 2-member union, inline
  function type) gets its parameters typed correctly, so the bug is in something the real one has on top: a
  non-const enum, a union of ~150 members, the generic alias `VisitEachChildFunction<T>`, named function expressions,
  or `SyntaxKind` members whose values are shared (`FirstX = X`). One root cause, probably a quick win. The other 42
  TS7006 (utilities.ts 26, checker.ts `getFalseType: (fresh?) =>` against optional/overloaded interface members) are
  contextual typing of object literal members.
- TS2345 317 + TS2769 103 + TS2322 92: calls. Known root causes in `triage.md` (`visitNode(node, visitor,
  isExpression)` picking the overload without the predicate, `Pick<CompilerOptions, ...>`, literal widening under
  a contextual type, `unknown` -> `Symbol`). Spread over checker.ts, esDecorators.ts, classFields.ts, program.ts.
- TS2349 78 ("not callable", emitter.ts 22, expressionToTypeNode.ts 12): not looked at.
- TS2339 79, TS18048 34, TS18046 21: aliased conditions / inferred `filter` predicates (`hacks.md` "Narrowing"),
  the `computedOptions` homomorphic mapped type in utilities.ts (~25), weak inference giving `unknown`.
- TS2495 25, TS2461 10, TS2556 12, TS7022 12: iteration protocol and `this` types (`triage.md` "Category-sized").
  If they are the last thing standing, they get their own plan (09) rather than a hack.

## Progress
**Session 1 (2026-09-22): 1751 -> 440**, suite 3496 -> 3571, no component lost that isn't explained in `hacks.md`.
One commit per root cause (`git log --grep "Plan 08"`): mapped types with `as` clauses (-694), inference to
`T & {}` (-292), optional calls, conditional `infer` inside function/object types + non-strict unions dropping
null/undefined + lazy conditional branches, TS2392 on constructor overloads, destructuring/property assignment
targets, Pick modifiers + intersection sources vs index signatures, reverse mapped types + Record inference +
`keyof` precedence, distributive conditional constraints, tuple rest spreads, aliased conditions, `-?`.

What is left (440), by root cause, biggest first:
- **Higher-order generic function inference** (TS2558 38, nodeFactory.ts `memoizeOne(<T>(kind: T["kind"]) =>
  ...)` returning a generic function): tsc's "propagated type parameters" in `inferFromSignatures` /
  `getOrCreateTypeFromSignature` when a generic function is passed where a non-generic single-signature function
  type is expected and the result is a function. Category-sized: its own plan.
- **Iteration protocol** (plan 09): TS2461/TS2495 ~25, and most of the `unknown`s (TS18046, TS2345 "'unknown' is not
  assignable", ~40): `arrayFrom(set)`, `arrayFrom(map.entries())`, destructuring iterables, for-of over `Set`/
  `Iterable<T>`.
- **Fresh vs regular literal types** (`hacks.md`, Expressions): `createComparator(operator, ...)` returns
  `{ operator: string }` because a parameter typed `"<" | ">="` widens like a literal expression (semver.ts 12).
  Needs fresh/regular pairs; also a plan (touches widening everywhere).
- Smaller, unexplored: TS2769 11, `JsxCallLike` properties (12, union narrowing by kind?), `Extract<NamedEvaluation,
  Pick<...>>` (8), `Mutable<T>` (6), `ResolutionWithResolvedFileName | (Resolution & {})` (4), TS2370 (4),
  `cache`/`top` possibly undefined (12: narrowing), `this` types (TS7022).

## How
This is a loop, not a design. Each iteration:
1. `./realworld.py`, pick the biggest bucket by *root cause* (group by message shape and file, read the source line).
2. Reduce it to a probe in `test/probes/plan08/` that `./filetest.sh` shows failing against tsc. If the reduction
   passes (like the `visitEachChildTable` one above), keep adding what the real code has until it fails.
3. Fix it the way tsc observably does it, in the general place (inference, contextual typing, relations), never by
   recognising the corpus' shape. `./fasttest.sh --summary` must not lose tests; a fix that moves the corpus and
   not the suite is fine, but check with `./resultdiff.py` that it doesn't just shuffle.
4. Commit per root cause (the numbers before/after in the message).

Stop and write a plan instead when a root cause is category-sized (more than a session, needs new `Ty`
machinery): the known candidates are `this` types, the iteration protocol with well-known symbols, and reverse
mapped types. Put it in `plans/` and move to the next bucket.

## Validation
- `./realworld.py` diagnostic count goes down per commit, `./fasttest.sh --summary` doesn't regress.
- At the end (or when the count is under ~100): `./bench.sh` and a callgrind profile of the corpus
  (`valgrind --tool=callgrind ./release/tttt -q -p thirdparty/tsc/src/compiler`), numbers in `measurements.md`.
  Expect the typecheck phase to grow now that calls stop failing early; that growth is the real perf baseline and
  the next perf plan starts from it.
- The plan is done at zero diagnostics, or when what remains is covered by other plans (listed here with counts).
