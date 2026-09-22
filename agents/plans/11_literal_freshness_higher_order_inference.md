# Plan 11 (sketch): fresh/regular literal types, higher-order generic function inference

A sketch, not an execution plan: a planning session turns each half into a real plan (or two) with probes, after
plans 09 and 10 have landed and the corpus has been re-measured. The two halves are independent.

## Fresh vs regular literal types
tsc has two `Ty`s per literal: the *fresh* one an expression produces (`"a"`, `1`, `true`, an enum member
reference) and the *regular* one a written type or a declared type holds. Widening only widens fresh literals.
tttt has one (`TYF_FRESH_LITERAL` is a TODO in ty.h), so it widens by position and guesses.
- Corpus: semver.ts 13 TS2345 + TS2678. `createComparator(operator: Comparator["operator"], operand)` returns
  `{ operator, operand }`; the shorthand property holds the parameter's *regular* union of literals, which tsc
  doesn't widen in the return type; tttt widens it to `string`.
- Suite/triage: `spreadBooleanRespectsFreshness`, single-member enums printing `E` where tsc shows the fresh `E.x`
  (hacks.md "Single-member enums"), `baseline_test_case_add_symbol_value` printing enum members by hand, a union
  filtered to an alias's members printing `string | false` for `FooBase`.
- Shape: `ty_literal` returns the regular type; a fresh twin (tsc's `freshType`/`regularType` pointers) made by
  literal expressions; `ty_widened_literal`/`widen_literals` only widen fresh ones; relations and interning treat the
  pair as the same type (tsc: `isTypeIdenticalTo` of fresh and regular is true, both have the same id in unions? check).
  Find every place that widens today (`check_for_mutable_location`, return type inference, `ty_widened_for_declaration`)
  and decide per place from tsc's `getWidenedLiteralLikeTypeForContextualType` & co.

## Higher-order generic function inference
tsc 3.4's rule: when a generic function is passed to a generic function whose parameter is a single-signature
function type, and the result is a function type, the argument's type parameters are propagated to the result
instead of being fixed (`inferFromSignatures` with a generic source signature, `getOrCreateTypeFromSignature`,
`instantiateSignatureInContextOf`, the `inferTypeParameters` return path in `resolveCall`).
- Corpus: TS2558 38, all nodeFactory.ts: `const getJSDocPrimaryTypeCreateFunction = memoizeOne(<T extends JSDocType>(kind:
  T["kind"]) => () => ...)` then `getJSDocPrimaryTypeCreateFunction<JSDocAllType>(...)`: `memoizeOne<A, T>(callback: (arg:
  A) => T): (arg: A) => T` must return a *generic* `<T extends JSDocType>(arg: T["kind"]) => ...`, so the explicit
  type argument is legal. Plus the nodeFactory.ts / parenthesizerRules.ts object-literal-with-generic-methods TS2322s.
- Suite: `genericFunctionInference1/2`, `inferFromGenericFunctionReturnTypes*`, TS2558 elsewhere; measure with
  `./failstats.py --code TS2558` and by name.
- Shape: inference context needs "inferred type parameters" (the source signature's own type parameters, moved to
  the result signature). `hacks.md` "`compare_signatures`: generic source signatures are unified positionally ...
  else erased to `any`" is the related relation-side gap: tsc instantiates the source in the context of the target
  (`compareSignaturesRelated` + `instantiateSignatureInContextOf`); probably the same machinery.
