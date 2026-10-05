# Plan 37: port the tsc mechanisms the long tail keeps working around
Brief by the insights of 2026-09-28; ranked and derisked by the planning session of the same day (todo entry 3),
which prototyped every candidate in a worktree (one Opus sub-agent each). **Every port below has a working prototype
at 3337e91 in `37_prototypes/*.patch`** (all apply cleanly to 3337e91, null_widening + union_origins together too),
with the measured gained tests next to it (`*_gained.txt`). Execution = clean up and land the prototype, not design.

## Why
Four windows of small fixes moved the suite from 8202 to 10215, and sessions kept ending with "the real fix is tsc's
X, which tttt doesn't have". Plans 29-30 showed porting a named tsc mechanism in place pays.

## Ranking (measured: full suite on the prototype vs 3337e91's 10217, 0 tests lost in every row)
| # | port | suite | corpora | HACKs deleted | sessions | risk | perf (instr, K=1) |
|---|------|-------|---------|---------------|----------|------|-------------------|
| A | non-widening null/undefined | **+48** (+4 improved) | none (all strict) | `widen_nullable`, `[]` display fake | 1 (+1 opt.) | low-med | +0.15% |
| B | union / keyof origins | **+37** (+47 components) | same counts; webpack messages print aliases | relation budget's only case, display.md origins | 1 (+1 opt.) | low | valibot **+3.0%**, zod +0.5% |
| C | decorators checked as calls | **+14** | nest's 532 decorators checked, still 0 | "decorators as expressions" + 3 TODOs | 1 | low | nest < 0.5% |
| D | evolving arrays (autoArrayType) | **+8** | unchanged (webpack's `var x = []` evolves now) | 3 (binding.c, ty/flow.c, flow.h) | 1 | low | +0.2% |
| E | missing type (exactOptional) | **+3** (+10 components) | zod-root +1 (its one missing TS2412) | `exact_missing_flag()` | 1 | low | -0.2..-0.8% |
| F | return-only signatures | +1 | fires 0 times | "no returnOnlyType" | 0.3 | very low | none |
| - | fixing without candidates | +1 / **-8** naive | zod +2 spurious TS2769 | (the gate) | 2-3 | high | ? |
| - | lazy reverse mapped types | ~1 | none | - | 1-2 | medium (perf) | cut 4 -> 12 hangs the suite |
| - | permissive instantiation | not measured | - | - | - | - | - |

A-E together: ~+110 tests for ~5 sessions, plus the hacks. The earlier estimates were off both ways: evolving arrays
"~100 tests" is really 18 failing tests with `x = []` (`let x;` already had TY_AUTO); null widening "~25" is 48.
Verdicts on the rest: **fixing without candidates, not now**: tttt decides up front which type parameters an argument
fixes by probing shapes (`fixed_by_argument`/`fixed_by_expr`/`fixed_by_type`/`fixed_by_function`), tsc fixes lazily
(only what instantiating a contextual signature for unannotated parameters touches); the port is `assign_contextual_
signature()` + `Checker.contextual_fixing_mapper` fixing lazily, then instantiateContextualType at instantiable
positions (the risky part), then deleting the probes, the "only where it would be unknown" stand-in and the gate.
It needs a precise "mentions type parameter P" walk (`collect_free_params` gives up on functions), which is also what
permissive instantiation's leftovers need. Losses to keep: `37_prototypes/fixing_gate_lost.txt`; zod's `Array.from({
length: n}, (_, i) => ...)` must stay clean. Measured gain +1: revisit when a corpus needs it. **Lazy reverse mapped
types, not now**: arrays/tuples are reverse mapped already (bugs/type_core.md's note was stale, fixed), only
reverseMappedTypeRecursiveInference plausibly needs laziness.

**Landed**: A stage 1 (+48) and F (+1) in todo entry 4, B stage 1 (+37) and E stage 1 (+3, zod-root's TS2412) in todo entry 5,
C stage 1 (+15) and D stage 1 (+21 with the early Array type) in todo entry 8.

## A. Non-widening null/undefined (landed at stage 1; stage 2 open)
tsc, non-strict only: `undefinedWideningType`/`nullWideningType` print as `undefined`/`null`, carry
ContainsWideningType, come from the `null`/`undefined` expressions, `void e`, bare `yield`, `[]`'s element, and a union
of only widening nullables; the flag spreads into unions, references and object literals; `getWidenedType` widens only
those to `any`. Prototype: `Ty.xflags` (`TYX_CONTAINS_WIDENING`, in padding), `TY_UNDEFINED_WIDENING`/`TY_NULL_WIDENING`
(aliases of the plain ones under strictNullChecks), OR'd in `intern_composite`, `ty_reference_with_this`,
`ty_object_literal`; `ty_union` keeps a union of only nullables widening unless a non-widening member is present;
made in `compute_inner`, `check_unary` (void), `check_array` (`[]`), `infer_generator_type`; `widen_nullable` exits
early without the flag. Three tsc widening sites tttt lacked (all 72 intermediate losses came from them): for-of
variables (`for_each_loop_statement_evaluate_var_type`), the object of a called/assigned property access (tsc's
`isMethodAccessForCall`: new `PropertyExpr.method_call`, set in the resolve walk's EXPR_CALL), and relate.c's
`isEmptyArrayLiteralType` (`[]` to `never[]` non-strict). Plus tsc's reportErrorsFromWidening under noImplicitAny
(`report_errors_from_widening` in binding.c: TS7018 / TS7005 / TS7006).
- **Stage 1 (1 session)**: land it; delete the `if (false)` branch and the dead helper the prototype left in the
  test traversal; skip the flag ORs under strictNullChecks; delete bugs/display.md's `[]`
  TS2345 entry; document `Ty.xflags` in documentation.md.
- **Stage 2 (optional)**, the remaining widening errors, ~8-10 tests read from diffs: TS7031 on binding elements
  (wideningTuples5, noImplicitAnyDestructuring*), TS7010/7011/7025 on widened inferred returns (wideningTuples7,
  implicitAnyGenericTypeInference), TS7008 class fields, TS7018 through a spread (noImplicitAnyUnionNormalizedObject
  Literal1: spread copies lose `literal_own`), `extends null` vs nullWideningType (classExtendsNull3), `export {
  undefined }` (reExportUndefined1), IIFE missing arguments.

## B. Union and keyof origins (landed at stage 1; stage 2 open)
Landed as the prototype described (`Ty.composite.origin`, documentation.md "Union origins"), +37 tests, 0 components
lost, corpora and threadcheck/checkerdiff unchanged. The perf cost (valibot +3% in the prototype) came from two
places, fixed: `U & U` was cheap in the old first-union distribution (it replaced every occurrence of the union at
once) but a |U|^2 cross product in tsc's flat one, and `keyof (A | B)` intersects `keyof A & keyof B`, which have
different origins now even with the same keys. `dedupe_unions()` drops a union whose members another union in the
intersection has (a display-only departure from tsc, hacks.md). Left: valibot +0.7%, zod +0.2% instructions.
- **Stage 2 (optional, ~5-15 tests, a guess)**: origins semantically: instantiate the origin in `instantiate_inner`'s
  union case (re-intersect for intersection origins: reverseMappedTupleContext, keyofIntersection,
  checkJsxChildrenProperty3), map over origin members in the union-mapping helper (tsc's mapType: `await`/narrowing keep
  `IteratorResult<number, void>`: privateNameMethodAsync), `keyof (A & B)` per getIndexType. Remaining list:
  `37_prototypes/union_origins_remaining.txt`. Not origins (triage): type-predicate narrowing should return the candidate unchanged
  (tsc's getNarrowedType: flowControlTypeGuardThenSwitch, partiallyDiscriminantedUnions, stringLiteralCheckedInIf02).

## C. Decorators checked as calls (landed at stage 1; stage 2 open)
Landed as the prototype described (documentation.md "Calls"): +15 tests (the prototype's 14 and missingDecoratorType,
TS2318 for the lib types a decorator signature needs, reported only once the callee isn't an error), corpora
unchanged (nest 0: its decorators are checked now), nest +0.4% instructions. Parameter decorators of class members are
checked by the class only (function_impl_typecheck skips them), so their contextual type doesn't depend on check
order. Probes in `test/probes/plan37/decorators/` (match tsc but for the symbol of a property of the ES context
intersection). Not cached per decorated node (each decorator is checked once; hacks.md `ty_check_decorator`).
- **Stage 2 (small, triage-sized)**: `this` inside an ES decorator is the outer `this` (outerThisReference x4,
  privateFieldAccess, decoratorOnClassMethod11: name resolution scoping). esDecorators-contextualTypes.2 needs `This`
  inferred through a generic decorator (inference). List: `37_prototypes/decorators_remaining.txt`.

## D. Evolving arrays (landed at stage 1)
Landed as the prototype described, cleaned up (documentation.md "Auto types and evolving arrays"): `TY_EVOLVING_ARRAY`
(element + cached final array), `EvolvingArrayOp` on IdentifierExpr, `auto_type()` in binding.c replacing the HACKs,
mutation nodes after `x?.push(v)` and through `x!` / `(x ||= [])`, a closure sees `const x = []` as the auto array,
the `x!` rules (auto: starts undefined, non-nullable; every identifier: only-nullish is the declared type). The side
finding is in too: the checker makes Array + the auto array when it starts (tsc's initializeTypeChecker), which is
what 14 of the gained tests needed. **+21 tests, 0 lost** (10477 -> 10498); every corpus byte-identical;
instructions (K=1) tsc +0.33%, webpack +0.29% (mostly layout: the evolving work itself is ~0.03%). IdentifierExpr's
rarer flags became bit fields: Expr went from 152 to 144 bytes (static_assert in expr.c). Probes in
`test/probes/plan37/evolving/` (all pass). Leftovers fixed in triage 2026-09-28 (regular object literals, TS7010 on widened arrays); the rest is in hacks.md.
Out of scope still: yieldExpressionInControlFlow (TS7005 from a nested evaluation).

## E. The missing type (landed at stage 1; stage 2 open)
Landed as the prototype described (documentation.md "The missing type"), `exact_missing_flag()` deleted; +3 tests,
zod-root 82 -> 83 (tsc's TS2412), other corpora unchanged; instructions valibot -0.1%, zod -0.7%, type-fest +0.1%.
- **Stage 2 (with other inference work, 2-3 tests)**: inferred candidates drop missing (contextuallyTypedOptional
  Property), inferring `T` from `Partial<T>` makes properties required (strictOptionalProperties1), JSDoc `[value]`
  optional properties (strictOptionalProperties3).

## F. Return-only signatures: landed (`return_only_type()` in ty/expr.c, async too; generators get anyFunctionType)
`inference.patch` stays for its fixing-without-candidates probe (`P37_FIX`) and reverse mapped depth toggle.
tsc's returnOnlyType under SkipContextSensitive: a non-inferrable signature whose return is the body checked against
the contextual return, cached under `KEY_CONTEXT_FREE`; `infer_from_signatures` skips non-inferrable parameters.
Fires in 6 suite tests, 0 corpus sites; +1 (badInferenceLowerPriorityThanGoodInference). Before landing: the shared
`p37_returns` buffer is reset by recursion (collect into a local), async/generator functions should get it too, drop
the getenv toggles. A side task for whichever session has room; deletes hacks.md "Skip-sensitive rounds"'
returnOnlyType part and bugs/type_core.md "Context-free type of return-sensitive functions".

## Order and validation
A, then B (they touch the same `intern_composite`: A ORs flags there, B adds the origin key; whichever lands second
rebases), then C + D (independent, one session could take both if C's stage 1 is quick), E, F as filler. Every stage:
`./fasttest.sh --summary` gains at least the listed tests with 0 lost, every corpus unchanged (E: zod-root +1),
`./threadcheck.sh` + `./checkerdiff.py` clean, `perf stat -e instructions:u ./release/tttt --checkers 1 --threads 1`
on zod/valibot (and nest for C, tsc/webpack for D) within the column above. The patches are against 3337e91: expect
small conflicts later, apply with `git apply -3` or by hand. Delete `37_prototypes/` entries as they land.
