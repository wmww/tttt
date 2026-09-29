# Plan 39: one resolution stack for circularity (planning stub)
Written by the 2026-09-29 triage session. **This is a stub for a planning session**: investigate, decide whether the
refactor is worth it, and replace this file with the real plan (or delete it and route the items to `bugs/` if not).

## The problem
tsc detects circularity with one stack (`pushTypeResolution`/`popTypeResolution`, `resolutionTargets`): a declaration's
type, a signature's return type, a base type, an alias's declared type... all push the same stack. A cycle found at
entry i marks *every* entry from i on as circular, so the outer declarations of the cycle get their TS7022/TS7023/
TS2502/TS2577 too, and each reports once, where tsc's `reportCircularityError` runs.

tttt has separate in-flight states that each only flag the one that was re-entered:
- LazyType's `LAZY_STATE_IN_FLIGHT` (`src/lazy_type.c`, the self-referential scheme; `TTTT_DEBUG_CYCLE=1` prints its
  chains), function return memos (`ty_function_return_in_flight`, `ty_signature_return_in_flight`), `Prop.circular`,
  `Checker.aliases_in_flight`, `defaults_in_flight`, `jsdoc_values_in_flight`, and `check_with()` (ty/expr.c) which has
  no cycle detection at all (`bugs/type_core.md` "Circularity").

## Tests (all fail on this, checked 2026-09-29)
Missing TS7022/TS2502/TS7023 at the outer declaration(s): for-of34, for-of35 (`v` when `next()`'s return type is the
cycle), circularTypeofWithVarOrFunc (TS2502 on `var x: Alias` whose alias is `typeof x`, TS2577), circularGetAccessor
(`typeof this.foo` in a declared class goes through the accessor LazyType, not the Prop), implicitAnyFromCircularInference,
recursiveTypesWithTypeof (also spurious TS2403 from the mismatch), circularReferenceInReturnType.
TS7023 tsc gets while printing a type whose return type is in flight: thisInObjectLiterals,
accessorInferredReturnTypeErrorInReturnStatement, trivialSubtypeReductionNoStructuralCheck,
checkingObjectWithThisInNamePositionNoCrash, conflictingDeclarationsImportFromNamespace1/2 (tttt's error printing
doesn't resolve members; maybe out of scope).
Reported too often: dependentDestructuredVariables (`function foo({ value1, test1 = value1.test1, ... })`: tttt gives
TS7022 on every testN, tsc once at value1 with TS7031; the file's 20 spurious TS2339 are a separate missing feature,
narrowing destructured variables by each other).
TS2313 (`walk_base_constraint` doesn't enter conditional types; `keyof` of an alias being resolved): recursiveMappedTypes,
circularBaseTypes, incorrectRecursiveMappedTypeConstraint; maybe the same stack, maybe not.

## Questions for the planning session
- Can the existing in-flight states push/pop a checker-level stack (entry = kind + key) without changing their memo
  layout, so the cycle marking is the only new behaviour? What does it cost per LazyType evaluation (they're hot:
  measure with `perf stat -e instructions:u` on the corpora)?
- tsc's stack is per checker; tttt runs several checkers (`--checkers 4`), each with its own: fine, but the
  `--checkers 1` vs 4 outputs must stay identical (`./checkerdiff.py`).
- Which errors move from the inner to the outer declaration, and does that change any passing test? Prototype on the
  7 tests above first.
- Validation: the tests above, 0 lost in `./fasttest.sh`, corpora unchanged (all 0 except webpack 6, xstate 19 at the
  time of writing), instruction counts within noise.
