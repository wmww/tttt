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
**Session 1 (2026-09-22): 1751 -> 365**, suite 3496 -> 3573, no component lost that isn't explained in `hacks.md`.
One commit per root cause (`git log --grep "Plan 08"`): mapped types with `as` clauses (-694), inference to
`T & {}` (-292), optional calls, conditional `infer` inside function/object types + non-strict unions dropping
null/undefined + lazy conditional branches, TS2392 on constructor overloads, destructuring/property assignment
targets, Pick modifiers + intersection sources vs index signatures, reverse mapped types + Record inference +
`keyof` precedence, distributive conditional constraints, tuple rest spreads, aliased conditions, `-?`,
discriminant-conflict intersection reduction, generic mapped targets + `T[K]` write types, `switch (x?.kind)`,
destructuring flow types, rest tuple parameters in signature relations.

**Session 2 (2026-09-22): 365 -> 239**, suite 3573 -> 3607, nothing lost. Root causes: `Mutable<T>` to T (mapped
type to type parameter rule), `x && y` with a generic x, binding element defaults under a written type + pattern
display, tsc's union signatures for union callees (getUnionSignatures), SkipContextSensitive overload passes,
fixing only the type parameters a lambda's parameters mention, homomorphic mapped types distributing over unions,
`x!` passing its contextual type on, const aliases of discriminants, tuple elements of intersections, tuple members
(`length`, array methods), boolean literals under a `boolean` contextual type (+9 suite), inferred type predicates
(TS 5.5), getNarrowableTypeForReference (constraint positions), contextual signatures from overloads/unions plus
discriminated object literal contexts. Also harness: `true`/`false`/`-1` literal type nodes in the types baseline
(+13 suite).

**Session 3 (2026-09-22, the small tail): 239 -> 203**, suite 3607 -> 3714, nothing lost that isn't explained in a
commit. Root causes: contextual rest parameters/tuple spreads/optional parameters (getRestTypeAtPosition,
getEffectiveCallArguments, addOptionality), `this` parameters + strictBindCallApply (CallableFunction), setters typed
by their getter, `x ||= a || b` flow, bottom-up flow queries typing lambdas without their assignment's context,
readonly properties as constant references, readonly mapped types (the parser lost `readonly` before a mapped key!),
nested generic calls seeing the outer inference (NoDefault clone), `readonly T[][]` precedence, TS2339 on arrays,
erased signatures for overloaded relations, object literals and optional properties in subtype reduction, `[]` as
never[], variadic tuple aliases, union inference priority (hit_priority), covariant-vs-contravariant tie rule,
undefined-returning functions, optional spreads, returnMapper for argument contexts + literal candidate union.

What is left (203), by root cause, biggest first:
- **Iteration protocol** (plan 09): TS2495 25, TS2461 10, TS18046 21, TS2769 3, and ~60 TS2345/TS2322 with
  `unknown` (`arrayFrom(map.values())`, for-of over `Set`/`Map`/`Iterable<T>`, destructuring iterables). Also the
  `TS7006 key` in builder.ts (arrayFrom) and `TS2365` in executeCommandLine.ts (reduceLeftIterator).
- **Higher-order generic function inference** (TS2558 38, nodeFactory.ts `memoizeOne(<T>(kind: T["kind"]) => ...)`,
  plus the nodeFactory.ts/parenthesizerRules.ts object-literal-with-generic-methods TS2322s): plan 10.
- **`this` types** (TS7022 13; resolutionCache.ts `perFileCache.set(...).get(...)` TS7006 x3): plan 09 or its own.
- **Fresh vs regular literal types** (semver.ts 13 TS2345 + TS2678): plan 10.
- Unexplored singles (~25): substitution types (TS2370 x4 in @types/node events.d.ts, `(...args: T[K]) => void` in
  the true branch of `T[K] extends unknown[]`, triage.md); `Mutable<T>`/`T & {...}` generic relations in nodeFactory.ts
  5073-7176; NodeArray<Node> vs NodeArray<ModifierLike> (parser.ts 3557/7976/9676, checker.ts 7968/50523: likely a
  `visitNodes`/`createNodeArray` inference); `Type | undefined` not narrowed (checker.ts 14692/18877/13348,
  moduleSpecifiers.ts 418/428); core.ts 2467 `[...any[]]` to T; commandLineParser.ts 4277; parser.ts 2414;
  classThis.ts 138; IdentifierNameMap `add` (MultiMap-style intersection); checker.ts 32308 reduceLeft overloads.

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
