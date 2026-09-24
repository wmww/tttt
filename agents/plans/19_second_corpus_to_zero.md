# Plan 19: the modern corpora to zero (loop, like plan 08)

`./realworld.py --corpus valibot` and `--corpus type-fest` (tsc 5.8 reports 0 on both; tsgo only 2 TS2554 in valibot
under its different moduleResolution). Work them down root cause by root cause, suite and the tsc corpus
(`./realworld.py`, must stay 0) as regression checks. Rank by root cause, not by code: sample the biggest groups,
reduce each to a probe in `test/probes/plan19/`, fix the tsc rule behind it. Each session: record both counts in the
commit message and `measurements.md`.

## Stage 0: module augmentation of an aliased export: done
`merge_augmentations` (`scope.c`); valibot 4473 -> 2169 (probe `test/probes/plan19/augment_alias.ts`).

## Known root causes after stage 0 (re-rank after plan 18)
- valibot after stage 0 (2169): TS2344 1166, most of them on expect-type's `toEqualTypeOf<X>()` (the argument checked
  against expect-type's mismatch type: `{ message: string; lang: ...; abortEarly: ... }` 227, `{ message: undefined;
  "~types": ... }` 138, the `"Expected: ${PrintType<...>}"` template 46): some relation/evaluation of expect-type's
  `StrictEqualUsingTSInternalIdenticalToOperator`/`DeepBrand` path, sample one before assuming plan 18 fixes it.
  TS2345 489: `Omit<SetIssue, ...>` not assignable to `Omit<SetIssue | StringIssue, ...>` (a homomorphic-ish mapped
  type over a union: distributes? relates?), object literals with `kind: string` (the `bytes.ts:104` return context
  below), TS2578 185, TS2322 141, TS1360 86 (`satisfies`).
- valibot `src/` (library code, not tests), plan 17 sample: most of it was `@ts-expect-error` lines (fixed in plan 17);
  what remained: an object literal returned from an overload implementation (`bytes.ts:104`: `kind: string` instead
  of `'validation'` against `BytesAction<...>` with `reference: typeof bytes`), `DeepPickN` (valibot's own mapped/
  conditional path picking, `partialCheck` tests, TS1360 on `satisfies`).
- type-fest `test-d/`: ~500 evaluation errors of utility types (`expectType<ArraySlice<[0,1,2,3],1>>([1,2,3])`: the
  utility evaluates to the wrong type). Most go through template literal arithmetic (plan 18). tsd's `expectType` /
  `expectError` are plain functions; `expectError` checks nothing at type level.
- tttt only works when run from the repo root (lib path relative to cwd): triage.md.

## Ranking after plan 18 (2026-09-23)
Totals: **type-fest 424** (TS2345 225, TS2339 75, TS2322 48, TS2578 35, TS2344 14, TS2589 13), **valibot 1965**
(TS2344 1204, TS2345 405, TS2578 185, TS2322 50, TS1360 44, TS2769 28, TS2339 24). The "known root causes after
stage 0" section above is stale: the expect-type TS2344 bucket is now explained (R1), and "most type-fest goes through
template literal arithmetic (plan 18)" was only half right: plan 18 made the templates evaluate, but the bulk of what
is left in type-fest is R5 (nested conditionals over `infer` variables), not template support. Repros below were
checked with `FAST=1 ./filetest.sh` against tsc; counts are estimates from grouping the full outputs (a diagnostic
can need several fixes, so buckets overlap a little). Roughly ordered by count per single fix (R7 bundles several
small missing checks).

### R1. ~~No identity relation: conditional types compared by pointer~~ done (valibot 620 -> 328, type-fest 195 -> 173; with tsc's intersection member matching in inference and InferencePriority as OR'ed flags, both needed for the expect-type trick)
`is_related_to` for `TY_CONDITIONAL` (relate.c) requires `source->conditional.extends == target->conditional.extends`;
tsc (structuredTypeRelatedToWorker) uses `isTypeIdenticalTo` on the extends types and relates check types both ways.
`types_identical` (relate.c) approximates identity as mutual strict-subtype. So expect-type's
`StrictEqualUsingTSInternalIdenticalToOperator<L, R>` (`(<T>() => T extends (L & T) | T ? 1 : 2) extends <T>() => T
extends (R & T) | T ? 1 : 2`) is false for *every* L = R, even `string`: after unifying the signatures' `T`s, `string & T`
and `{a: T}` are rebuilt, not interned, so the pointers differ. Simple types still pass only because expect-type falls
back to `MismatchInfo<A, E>` (= A when they mutually extend); anything with a method or optional property fails (952
TS2344 whose constraint is a `{ ...: MismatchInfo<...> }` object). Size: medium (a real identity relation, tsc's
`identityRelation` in isRelatedTo/structuredTypeRelatedTo: same flags, unions by each-member, objects property-by-
property with identical types and modifiers, signatures via compareSignaturesIdentical; plus maybe interning
intersections/anonymous instantiations). Perf: every `toEqualTypeOf` currently evaluates the whole `DeepBrand` graph
(of RegExp, Promise, schemas...) in the fallback; fixing R1 probably takes a real bite out of valibot's 4.4 s.
```ts
type SE<L, R> = (<T>() => T extends (L & T) | T ? 1 : 2) extends <T>() => T extends (R & T) | T ? 1 : 2 ? true : false;
type A = SE<string, string>;                                               // tttt false, tsc true
type B = (<T>() => T extends {a: T} ? 1 : 0) extends (<T>() => T extends {a: T} ? 1 : 0) ? true : false; // false
```

### R2. ~~Homomorphic mapped types over a union/intersection lose `?`~~ done (valibot 1628 -> 739: far more than the ~190 estimated, most expect-type mismatches were this)
`Pick`/`Omit`/`Prettify` whose modifiers type is `A | B` or `A & B` make every property required. tsc: the mapped
property's optionality comes from `getPropertyOfType(modifiersType, key)`, which for a union is optional if optional in
any member, for an intersection only if optional in all members that have it (createUnionOrIntersectionProperty).
166 TS2345 `Omit<SetIssue, "input" | "received">` not assignable to `Omit<SetIssue | StringIssue, ...>` ("Property
'lang' is optional in ... but required in ..."), plus valibot's `InferObjectInput` = `Prettify<Partial<X> &
Required<Omit<X, K>>>` making `exactOptional`/`optional` keys required (`({} | {key: string})[]` not assignable,
~25). Probably some type-fest (`Simplify`, `UndefinedToOptional` in jsonify) too. Size: small.
```ts
type P<T> = { [K in keyof T]: T[K] };
const f: P<{ key?: string } & { b?: 1 }> = {};                         // tttt TS2739 (key, b missing)
interface A { lang?: string; k: 1 } interface B { lang?: string; k: 2 }
declare const a: Omit<A, 'k'>; const b: Omit<A | B, 'k'> = a;           // tttt TS2322
```

### R3. ~~`keyof` of an object literal type containing any function is deferred~~ done (type-fest 295 -> 198, valibot 1853 -> 1628; with the failed-conditional-check walk and tsc's getBaseSignature)
`keyof typeof {a: () => 1}` stays `keyof {...}` (also `E[keyof E]`, and conditionals over such types stay deferred:
valibot's `StrictEqual...<NullishSchema<..., () => undefined>, ...> extends true ? ...` constraint, 212 TS2344 are
this shape). Function *expressions*/methods inside an object literal only; `keyof {a: typeof f}` is fine. Looks like
the object literal is flagged as possibly containing type variables (tsc's couldContainTypeVariables is true for an
anonymous function type only when it has outer type parameters in scope). Explains type-fest partial-deep and
readonly-deep TS2339 (`PartialDeep<typeof foo>` where `foo` has `function: (_: string): void => undefined`; ~80),
valibot `partial`/`required` tests (`Schema` param TS2345 39, TS2769 28, their cascading TS2344). Size: small-medium.
```ts
const e = { a: () => 1 };
type K = keyof typeof e;                                                  // tttt keyof {...}, tsc "a"
```

### R4. ~~`const` type parameters are ignored~~ done (valibot 739 -> 620, with `as const` being the regular literal type)
`parse_generic_decl_item` drops the `const` modifier (hacks.md). tsc checks the argument in a const context
(isConstContext: contextual type is a const type variable) and never widens its inference. Every valibot factory is
`<const TWrapped ..., const TDefault ...>`. Effects: `getDefault(exactOptional(string(), 'foo'))` is `string` (40
TS2344 "Expected: literal string: foo, Actual: string"), `partial(schema, ['key1','key3'])` infers `string[]`, so
`TKey extends TKeys[number]` picks the wrong branch (TS1360 `satisfies` 44 and their TS2345), `let x = id('foo')`
is `string`. Size: small-medium (parse + store the flag, const context for arguments, skip widening in
getInferredType, readonly tuples for array literals).
```ts
declare function q<const K extends readonly string[]>(k: K): K;
const s = q(['a', 'c']);                                  // tttt string[], tsc readonly ["a", "c"]
declare function id<const T>(t: T): T; let r = id('foo'); // tttt string, tsc "foo"; id(1) in const W -> number
```

### R5. ~~`infer` variables are not treated as generic in nested conditionals~~ done (type-fest 450 -> 295, valibot 1871 -> 1853)
In the true branch, a conditional whose check or extends type mentions an outer `infer` variable is resolved on the
spot instead of deferred and re-evaluated after the branch is instantiated: `string extends T` (T infer, constraint
unknown/string) resolves to true, and `S extends `${A}${infer U}`` (A outer infer) infers A afresh ("f"). tsc: the
conditional is deferred because `isGenericType(extendsType)`; instantiating the true branch with the inference mapper
then resolves it. Covers type-fest StringToArray/StringLength (so string-slice, array-slice `[]`/`never`, array-splice,
tuple-min/max, int-range, greater/less-than(-or-equal) via PositiveNumericStringGt), SplitIncludingDelimiters (the
`never` of delimiter-case, snake/kebab/screaming-snake-case ~30), IsLiteral (`[LiteralType] extends [U]`), IsUnion
(`boolean extends Result`), probably camel-case's TS2589 (wrong split recursing). Size: small-medium, high leverage.
```ts
type X<S> = S extends [infer T] ? (string extends T ? 'wide' : 'narrow') : 0;
type A = X<['1']>;                                                   // tttt "wide", tsc "narrow"
type N<S extends string> = S extends `${infer A}-${infer B}` ? (S extends `${A}${infer U}${B}` ? [A, U, B] : 0) : 0;
type B = N<'foo-bar'>;                                               // tttt ["f","o","o-bar"], tsc ["foo","-","bar"]
```

### R6. ~~Literal widening ignores template literal contextual types (valibot 128)~~ done at the end of plan 18 (valibot 1965 -> 1835)
A string literal is kept when the contextual type contains a string literal *or a template literal / string mapping*
(tsc isLiteralOfContextualType). tttt widens `'!1'` against `` `!${string}` `` (valibot's notValue(s)/ltValue/gtValue/
minValue/maxValue issue objects, 100 TS2345 + 24 TS2322). Size: small.
```ts
declare function f(b: { expected: `!${string}` }): void;
f({ expected: '!1' });                                   // tttt TS2345 (string not assignable), tsc ok
```

### R7. Missing checks behind unused `@ts-expect-error` (valibot ~150 of TS2578 185, type-fest ~16)
- TS2352 (as-conversion without sufficient overlap) is not implemented (`EXPR_AS_TYPE` TODO in expr.c): valibot's
  `return dataset as OutputDataset<...>` ~60. Needs the comparable relation both ways. Small-medium.
- TS2540 (assign to readonly property) not implemented: `issue.path = [pathItem]`, `func.schema = ...` ~50. Small.
- TS7053 (implicit any element access, `input[key]` with `input: object`/`{}` under noImplicitAny) ~25. Small.
- TS2314 (generic type used without its type arguments, `type A = IsAny;`, TODO in lower.c): type-fest ~15. Small.
- The rest: overloaded `args` call arity, DistributedOmit constraints, vitest spies; not sampled.

### R8. Smaller evaluation bugs (type-fest ~40, valibot ~30)
- `O['condition']` instantiated with `O = {}` (optional property not present) stays deferred; tsc gives `unknown`
  (conditional-pick-deep's `AssertCondition`, ~16 TS2345). `type G<O extends {c?: 1}> = O['c']; type C = G<{}>`. Small.
- Spurious TS7022 (so `any`, then TS2339 at use sites) under `strict` reading a property of a recursive
  conditional-to-mapped type with `?`: `type PD<T> = T extends object ? POD<T> : T; type POD<T extends object> =
  { [K in keyof T]?: PD<T[K]> }; declare const q: PD<{bar: {n: number}}>; const c = q.bar;` (only with
  `// @strict: true`). Part of partial-deep / required-deep / partial-on-undefined-deep. Small-medium.
- Jsonify: `declare const o: Jsonify<string>; const s: string = o;` reports TS2589 (but `{x: Jsonify<string>}` shows
  `string` correctly), and `JsonifyObject<UndefinedToOptional<{keep: string; u: undefined}>>` lacks `keep` (maybe R2).
  Unreduced, ~15.
- valibot TS2589 at `dataset = this.wrapped['~run'](dataset, config)` in nonNullable/nonNullish/nonOptional (6):
  tsc reports an ordinary TS2322 there. Unreduced.
- vitest `expect(x).not` TS2339 (12): `interface Assertion<T> extends VitestAssertion<Chai.Assertion, T>` where
  VitestAssertion is `{ [K in keyof A]: ... } & ((type: string) => Assertion)`; members of an interface whose base is
  an intersection containing a mapped type. Unreduced.

### Display bugs seen on the way (no count change, but they break suite baselines)
- ~~An alias name gets attached to a type the alias only *evaluates* to (`J<X> = X['t']['i']`)~~: fixed at the end of
  plan 18 (the renaming rule now follows the alias's declared type, see hacks.md).
- `type PI = 1e999` prints `NaN` (tsc `Infinity`; type-fest's PositiveInfinity/NegativeInfinity).
- `g(() => 1)` with `g<T>(t: T): T` infers `() => number` (tsc `() => 1`: the return literal isn't widened when
  the contextual return type is a type parameter).

### After R5, R3, R2 (2026-09-24)
type-fest **197** (TS2345 120, TS2578 29, TS2322 15, TS2589 15, TS2344 9), valibot **739** (TS2344 443, TS2578 125,
TS2322 73, TS2339 24, TS2741 22, TS2345 21). Sample of valibot TS2344 by constraint: `"Expected: literal string: foo,
Actual: string"` 27 and `{ 0: "foo"; 1: 123; 2: true; }` 20 (R4), `StrictEqualUsingTSInternalIdenticalToOperator<...>`
and `MismatchInfo` objects (R1) most of the rest. valibot's partial/required tests now reach `satisfies` (TS1360 10,
R4). Re-rank type-fest's TS2345 after R4/R1, it hasn't been sampled since plan 18.

### After R4, R1 (2026-09-24)
type-fest **173** (TS2345 107, TS2578 29, TS2322 13, TS2589 13, TS2344 6), valibot **328** (TS2578 129, TS2344 96,
TS2322 30, TS2339 24, TS2345 15, TS2488 9, TS18046 9). valibot's TS2344 are still expect-type (82 of 96 name
`StrictEqualUsingTSInternalIdenticalToOperator`), but no longer the identity relation: sampled ones are R9. TS2578 is
R7 (missing checks). TS2339 24 is R8's vitest `.not`. type-fest wasn't re-sampled (spread thin: conditional-pick-deep
16, get 10, camel-case 10...).

### R9. ~~`infer` type parameters have no implied constraint from their position~~ done (valibot 311 -> 245, type-fest 173 -> 167 with sliceTupleType's mutable rest slice; the permissive instantiation was not done, see hacks.md `ty_conditional_resolve`)
`S extends SchemaWithPipe<infer TPipe> | SchemaWithPipeAsync<infer TPipe> ? ...` with nothing inferred: tsc gives TPipe
the constraint of the type parameter it is an argument of (getInferTypeParameterConstraint / the implied constraint),
and an inference that doesn't satisfy it is the constraint. tttt infers `unknown`, so the extends type becomes
`Omit<FirstTupleItem<unknown>, ...> & {...}`, a generic mapped type, and `ty_conditional_resolve` defers (it can't
pick the false branch without a permissive instantiation). Deferred key conditionals leave valibot's
`InferObjectOutput` mapped types generic, so `IsNever<Omit<ObjectSchema<...>>>` defers and so does every
`toEqualTypeOf` over such a schema. Probe `test/probes/plan19/infer_implied_constraint.ts` (tsc `A = "no"`, tttt
deferred). Only `imply_rest_constraint` exists (the `unknown[]` of a rest position). Size: small-medium. Worth
considering with it: a real permissive instantiation (every type parameter to a wildcard `any`) instead of the
`mentions_type_variables` walk, which is the other half of why these defer.

### Suggested order
~~R5 and R3, R2, R6, R4, R1, R9~~ (done). valibot's 30 TS2344 left are unrelated expect-type mismatches (readonly
members `key04`, `Partial<Record<...>>` index signatures, `[]` vs array methods, Promise `then`): re-sample. Then the R7 checks one by one (valibot's
TS2578), R8's vitest `.not`. Re-sample type-fest's TS2345 before picking its next root cause.
