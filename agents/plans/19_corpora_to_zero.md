# Plan 19: the corpora to zero (loop, like plan 08)

**Status 2026-09-27 (after plan 33)**: tsc 0, **valibot 0**, **react 0**, **nest 0**, **webpack 3981** (checkJs,
nearly all JSDoc typedefs: plan 26 stage 3, section "webpack corpus" at the end), **type-fest 19** (shared-union-fields-deep 8 = R17, conditional-pick-deep 6 = R20, abstract-class 4 = plan 32,
jsonify:57 1 = the missing type). The newest section is "Corpus session of 2026-09-27" at the end; the older ones are
history (their counts are stale).

`./realworld.py --corpus valibot`, `--corpus type-fest` and `--corpus react` (tsc 5.8 reports 0 on all; tsgo only 2
TS2554 in valibot under its different moduleResolution). Work them down root cause by root cause, suite and the tsc corpus
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
- ~~tttt only works when run from the repo root~~ fixed (`main.c` finds the lib).

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

## React corpus (plan 25, 2026-09-26): 362 -> 80
Fixed in plan 25's session: `catch (e: any)` didn't parse (and catch variables are now unknown under
useUnknownInCatchVariables), `typeof x ? {...} : never` parsed a ternary as the type query's operand, `import { type,
a }`, interfaces extending an intersection alias inherited nothing (lucide's `LucideProps`, 46; also valibot -17),
exports conditions didn't fall through to the next condition when a target doesn't resolve (class-variance-authority,
TS7016 then `any` everywhere), a fresh `{}` was a subtype of weak types (`config || {}`), `[T] extends [W]`
conditionals weren't deferred (Storybook's `StoryObj`, 19). Probes of all of these are in `test/probes/plan25/`.
What remains, by root cause:

### R10. ~~First inference pass skips a whole object literal with a context-sensitive member~~ done (react 80 -> 29; the rest of that session's fixes, below, took it to 3)
tsc checks such an argument with `CheckMode.SkipContextSensitive` in the first pass: only the context-sensitive
function members become the non-inferrable `anyFunctionType`, the other members (`schema`, `mutationFn: deleteUser`)
still infer. tttt `continue`s past the whole argument (`call.c` first-pass loop: `if (arg->context_sensitive ||
arg->generic_call) { any_sensitive = true; continue; }`, `check_args(..., skip_sensitive)`, `waits_for_sensitive()`);
JSX attributes are one such argument (`call.c` ~1461). So type parameters are fixed with nothing inferred: react-hook-
form's `<Form schema=.. onSubmit={(values) => ..}>` gets `TFormValues = FieldValues` (22 `FieldError | Merge<...>`,
9 attribute TS2322, the `FieldValues` TS2345s), react-query's `useMutation({ onSuccess: (...args) => .., mutationFn })`
gets `TVariables = void` (7 TS2322 + 7 TS2345 at `.mutate(x)` + 7 `unknown` data). Size: medium (a skip-sensitive
mode in `ExprCtx`: bypass `check_with()`'s memo, type sensitive function members as a non-inferrable any-function
without fixing their parameters; then infer from the result). Probes `plan25/rhf/skip_sensitive_objlit.ts` (minimal),
`rhf/formdefault.ts`, `rhf/mutation.ts`, `jsx/generic_component_sensitive_attrs.tsx`,
`misc/context_sensitive_member_before_inference_source.ts`. triage.md's older "inference from object literals with
context-sensitive members" item is this.
```ts
declare function g4<S>(p: { s: S; cb: (v: S) => void }): S;
const b4 = g4({ s: 1, cb: (v) => {} });   // tsc number, tttt unknown
```

### R11. ~~Intra-expression inference~~ done for arrow functions only (hacks.md: methods would need `this` not to be memoized)
Both members context-sensitive: tsc checks them in source order in the second pass and infers after each, so
`queryFn`'s return type feeds `getNextPageParam(lastPage)` (TS 4.7). tttt checks the literal as a whole. 3x TS18046
`'lastPage' is of type 'unknown'` (useInfiniteQuery). Probes `rhf/intra.ts`, `misc/context_sensitive_members_in_order.ts`.
Size: medium.

### R12. ~~The apparent type of an intersection has `this` bound to the intersection~~ done in `find_prop` (react 10: Storybook `component:`)
`(ReactRenderer & { T: TArgs })['component']` where `component: (props: this['T']) => ..`: tsc's
getApparentTypeOfIntersectionType is `getTypeWithThisArgument(type, type)`, so `this['T']` is `TArgs`; tttt gives
`unknown` (Storybook `Meta`'s `component:`). `apparent_type()` (relate.c) has no intersection case, and
`ty_with_this_arg` returns early when `ty == this_arg`. Probe `misc/this_type_through_intersection_indexed_access.ts`.
Size: small-medium (cache the result).

### R13. ~~Indexed access on a missing property instantiates to unknown~~ done (`ty_indexed_instantiated`; also type-fest -17, valibot -4)
falso's `type Return<T, O> = [O] extends [never] ? T : O['length'] extends number ? T[] : T` with `O = { withAccents:
false }`: `O['length']` instantiated without an access node is `unknown` in tsc (getIndexedAccessType), tttt keeps it
deferred (`try_resolve_indexed`/`ty_indexed`, checker.c), so the conditional never resolves. Keep the error at
declaration sites. Probe `misc/falso_return_conditional.ts`. Size: small. Probably also in type-fest/valibot (R8's
`O['condition']` with `O = {}` looks like the same rule).

### R14. ~~Implicit index signature inference from intersections~~ done
`Object.entries(x)` with `x: Mapped & { [SYMBOL]: .. }` (@mswjs/data's `FactoryAPI`): tsc's
isObjectTypeWithInferableIndex accepts intersections of inferable object types and only counts properties whose key
fits the index key type (symbol keys don't count for a string index). tttt's implicit-index branch in
`infer_from_objects` (infer.c) requires `TY_ANONYMOUS` and doesn't filter keys. Probe
`misc/entries_of_intersection_with_symbol_key.ts`. Size: small.

### R15. Conditional types deferred on a check type that only mentions an outer type parameter (react 1)
`L1<(x: E) => void, {e: E}>` inside a generic function stays deferred: tsc only defers when the check type is a
generic object/index type, and otherwise tries the permissive instantiation (type parameters -> wildcard) to pick the
false branch. `ty_conditional_resolve`'s `mentions ? nullptr : ...` HACK (hacks.md). `LibraryManagedAttributes` of a
generic component inside a generic component (`table.tsx`). Probe `jsx/lma_deferred.tsx`. Size: medium; the same
permissive instantiation R9 left undone.

Unreduced then, now R16 below: `form.tsx:198/201` (2 TS2345, `useForm({ ...options, resolver })`).

### Suggested order (react)
~~R10, R12/R13/R14, R11~~ (done), R15: see the newest section.

## After the R10-R14 session (2026-09-26)
Also fixed on the way (probes in `test/probes/plan19/`): `mentions_type_variables` expanding recursive conditionals
(`typeof GenericComponent extends {...}`, React's LibraryManagedAttributes), aliases of `readonly [...unknown[]]`
(type-fest Spread), conditional types over a generic mapped check type deferring (type-fest Paths: PickDeep 24), the
constraint of an indexed access on an intersection (valibot InferInput/InferIssue, 12+), overload order across merged
interface declarations (Promise.all's tuple overload, valibot recordAsync). What is left:

### R16. Generic functions nested in a literal argument: skipped, then instantiated with the fixing mapper (react 2)
`useForm({ ...options, resolver: zodResolver(schema) })` in a generic component: `zodResolver()` returns a *generic*
function. tsc's first pass checks it with SkipGenericFunctions (it becomes anyFunctionType), so V is inferred from the
spread alone (T); the second pass instantiates it in the context of `Resolver<V, C>` via instantiateSignatureInContextOf
with the *fixing* mapper, which fixes V = T before the resolver's own FieldValues candidates could count. tttt only does
this for top-level generic function arguments (`Arg.generic_function`). Extend `ExprCtx.skip_sensitive` to generic
functions (skip generic function-typed members, identifiers included, whose contextual type is a single non-generic
signature), and in the second pass instantiate them in context with fixing. Probe
`test/probes/plan19/generic_function_in_literal_fixes_outer.ts` (a1 a2 a4 a5 should be T, a3 FV). Size: medium.

### R7 again: TS7053/TS7052 are not implemented (valibot ~27 of 40 TS2578; suite 71 missing in 23 tests)
Most of valibot's unused `@ts-expect-error` sit on `input[key]` with `input: object`/`{}` and `key: string`: tsc's
TS7053 "Element implicitly has an 'any' type because expression of type 'string' can't be used to index type '{}'"
(noImplicitAny, getIndexedAccessType's error path; TS7052 is the "Did you mean to call 'c.get'?" variant). A new
check in element access, suite-visible too (`./failstats.py --code TS7053`: noImplicitAnyStringIndexerOnObject,
indexSignatures1 23...). Size: small-medium. The other valibot TS2578 lines: `value1[key] = value2[key]` (probably the
same), overload implementation compatibility (TS2394-ish, 2), `getDefault(schema)` returns (unsampled).

### Unsampled
type-fest TS2345 79 spread thin: shared-union-fields-deep 8, readonly-deep 7, conditional-pick-deep 7,
last-array-element 6, set-parameter-type 5, partial-on-undefined-deep 5, find-global-type 5; camel-case TS2589 7,
pascal-cased-properties-deep TS2322 6, package-json TS2322 5. valibot: keyof.test-d TS2344 5, `_addIssue` TS2339 4,
pipeAsync TS2339 3. react's last: table.tsx (R15).

### Suggested order
TS7053 (a missing check, valibot's biggest bucket and suite-visible), R16, then sample type-fest's TS2345 files
(one root cause per file is typical), R15 (the permissive instantiation R9 also wanted).

## After the TS7053/R16 session (2026-09-26)
Done: TS7053/TS7052/TS7015 (`ty_check_element_implicit_any`, with a real `TY_RELATION_SUBTYPE`, for-in key types,
tuple unions), R16 (`GenericFunctionMode`), and from subagent reductions of type-fest (probes in
`test/probes/plan19/typefest/`, one file per cause): tuple inference/relation ported from tsc (leading variadics),
intersection-source signatures in inference, mapped types with `as` over primitives and over rest tuples, numeric
property names in `keyof`, tail-recursive conditional types (`ty_instantiate_tail`), `ty_indexed_instantiated`'s
R13 overreach, callables without implicit index signatures, primitives against `{ [k: string]: any }`,
`Record<any, X>`, homomorphic mapped types over index signatures and intersections. What is left, by cause:

### R17. Implied constraints make `{ [K in keyof U]: ... }` non-homomorphic (type-fest shared-union-fields-deep 8)
`type SO2<U> = keyof U extends infer Keys ? { [K in keyof U]: U[K] } : 0` with `U = A | (A & {foo: any})`: tsc
`{ a: string }`, tttt distributes (`{ a } | { a; foo }`). In the true branch tsc's getConditionalFlowTypeOfType makes
`keyof U` a substitution type (constrained by `Keys`), and getHomomorphicTypeVariable needs an Index-flagged
constraint, so the mapped type isn't homomorphic. tttt decides homomorphism from the syntax (`in_type.tag ==
TYPE_KEYOF`, `instantiate_mapped_over_list`) and has no substitution types. Probe `typefest/suf_dist.ts`. Size:
medium (the narrow version: a mapped type whose `keyof X` constraint is the check type of an enclosing conditional
with an `infer` extends isn't homomorphic).

### R18. Re-lowering a type node that is in flight gives the error type (type-fest conditional-pick-deep 6)
**-> plan 29 (2026-09-27), which found a different cause**: an eagerly lowered mapped type template, not alias
re-entry; conditional-pick-deep is a separate evaluation bug (R20 below). The text below is the original diagnosis.
`type Pk<K extends string> = {[P in K]: 1}; type P<T> = Pk<keyof M<T> & string>; type M<T> = { [K in keyof T]:
P<T[K]> }`: `P<{o: 1}>` stays `P<T>`. Checking `Pk<...>` against Pk's constraint (only done for constrained
parameters, `ty_check_type_reference_constraints`) lowers the argument node before P's declared type exists; that
reaches P's declared type through M, which lowers the same node again while it's in flight and gets TY_ERROR, so P's
type isn't generic. Forcing the alias's declared type first in `type_decl_typecheck` fixes the probe but breaks the
DelimiterCase family (type-fest 53 -> 73, `Uppercase<any>`: another in-flight cycle), so it was reverted. The real
fix is tsc's: alias instantiation and type references that are resolved lazily (isDeferredTypeReferenceNode /
mayResolveTypeAlias defer array, tuple and generic references in aliases), rather than a memo that returns an error
when re-entered. Probes `typefest/alias_constraint_cycle.ts`, `pick_rec.ts`, `cpd_opt.ts`. Size: large; a plan of
its own, also for R19.

### R19. Eager instantiation of recursive aliases: TS2589 where tsc has none (type-fest readonly-deep, writable-deep, ts41)
**-> plan 29** (deferred generic interface references in alias bodies; ts41's TS2589 is R21 below).
`type RD<T> = T extends ReadonlyArray<infer I> ? ReadonlyArray<RD<I>> : T; type J = string | J[]; RD<J>`: tsc defers
`ReadonlyArray<RD<I>>` (a generic interface reference in an alias whose argument may resolve an alias), tttt expands
it until the limits. Also: `report_instantiation_limit` reports at the *next* assignability check, which may be
unrelated code (the probe's TS2589 lands on `const x: number = 1`). Probe `typefest/rd_2589_min.ts`. Same plan as R18.

### R20. conditional-pick-deep (type-fest 6), found by plan 29's investigation
Not re-entry and no limit (gdb): the nested `ConditionalPickDeep<{string: string}, string>` evaluates to something
`ConditionalKeys` drops, and `Inner["object"]` prints `Inner`. Probe `test/probes/plan29/cpd_steps.ts` (tsc itself is
order-dependent there: `sub: any`).

### R21. TS2589 from type-fest's `ArraySlice` (fixed by plan 29 stage 3; what it uncovered)
Reported at the real site since plan 29 stage 3 (15 in array-slice, 8 in string-slice, 1 in int-range). Cause:
`[...BuildTuple<A>]['length']` resolved eagerly to `number` (no isGenericTupleType), so `Sum<1, 1>` was `number` and
`ArraySliceByPositiveIndex` never ended; fixed, as was the tail count spanning non-tail instantiations. Left, now
visible (they were behind the error type):
- array-slice:24/25/34 (TS2322): `ArraySlice` of variable-length tuples (`[...string[], 1, 2]`) gives `string | 1 | 2`
  where tsc has the right element.
- int-range:17 (TS2589): `IntRange<0, 999>`.
- jsonify:57 (TS2345, `test/probes/plan29/jsonvalue_missing_type.ts`): no exactOptionalPropertyTypes *missing* type;
  the widened `{}` gets `a?: undefined` where tsc's getUndefinedProperty gives the missing type, which index
  signatures ignore. Was hidden by JsonObject's index signature being the error type (lowered while in flight).
- writable-deep:93/94 (`test/probes/plan29/writable_deep_tuples.ts`): `lower_tuple` keeps a rest element's written
  array type, so `ReadonlyDeep<[string, ...number[]]>` is `readonly [string, ...readonly number[]]` and WritableDeep
  of that `[string, ...unknown[]]`. Order-dependent (direct `WritableDeep<readonly [string, ...number[]]>` is right).

### Also seen
- Suite: spurious TS7053 where tttt lacks the rule, not the check: index signatures keyed by tagged strings
  (`[k: TaggedString1]` indexed by `string & Tag1`, indexSignatures1 8), `in` narrowing (inKeywordTypeguard 8,
  narrowingMutualSubtypes 4). JS expando TS7053s (lateBoundAssignmentDeclarationSupport*) wait for plan 26.
- `keyof 1` stays a deferred keyof (for-in over a primitive needed a genericity check in `ty_for_in_key_type`).
- Display: `X<string | number>` for a distributive alias over a union keeps the alias (tsc only aliases a distributed
  union for a new alias symbol, `distributed_union_alias.ts`); `P<U>` where tsc prints the indexed access it resolves
  to (`indexed_prop_is_indexed.ts`).
- Unsampled: type-fest package-json 5+2, find-global-type 5, exact 3+2, merge 2, get TS2349 2; valibot TS2322 12,
  TS2578 10, TS2344 10.

### Suggested order
R18/R19 are plan 29 and R17 was plan 30 stage 3 (done 2026-09-27): run those as their own sessions. For plan
19 sessions: R20, R21, sample valibot (unsampled since the R10 session) and the rest of type-fest; R15 (react's last,
the permissive instantiation; plan 30 stage Q adds the restrictive half of the same machinery).

## NestJS corpus (plan 31, 2026-09-27): 134 -> 0, done the same day
`./realworld.py --corpus nest`: brocoders/nestjs-boilerplate (NestJS 11, TypeORM + Mongoose, class-validator,
passport, nestjs-i18n, aws-sdk v3, supertest/jest e2e tests; experimentalDecorators + emitDecoratorMetadata,
CommonJS, node10 resolution, 2394 files / 259k lines with dependencies). Before the first run could happen: STC's
quicksort crashed tttt at startup (`thirdparty/stc-sort-pivot-patch.txt`), and `export interface` inside `declare
global {}` (@aws-sdk/types) was a parse error, which silently turns off checking without `--keep-going`. Fixed the
same day (probes in `test/probes/plan31/`):
- Named imports from an `export =` module take the property of the assigned value's type too (tsc's
  getExternalModuleMember/combineValueAndTypeSymbols; `SymbolAlias.value_target`). superagent's `import { Request }`
  is a type-only namespace interface plus `Request: typeof SARequest` on the value; supertest's `class Test extends
  Request` extended the global fetch `Request` (~85 diagnostics, all the e2e tests). `exportEqualsPropertyBase.ts`.
- `await` unwrapped only `Promise`/`PromiseLike` by name; now any thenable (tsc's getPromisedTypeOfPromise in
  `awaited_type_in`). superagent's requests and every mongoose `Query` (~20). `awaitCustomThenable.ts`.
- tsc's instantiateMappedType works on the mapped type's root with the combined mapper: the app's own recursive
  `DeepPartial<T[P]>` never distributed over `string | null` (8). `wtb_deepPartial.ts`.
- getDefaultConstructSignatures: a class extending a non-class base (aws-sdk's `declare const X_base: { new (...) }`)
  inherits the base expression's construct signatures (2). `wtb_classExtendsConstCtor.ts`.
- isLiteralOfContextualType asks maybeTypeOfKind(baseConstraint, String), through unions: `P extends Path<T>` (nest's
  `ConfigService.getOrThrow('app.corsOrigins', { infer: true })`) kept the literal only if the base constraint was
  `string` itself, so PathValue gave never (5: the `ms()` overload and `corsOrigins.includes`). Also regularizes the
  candidate in `inference_call_result`, since tttt doesn't do it when checking the argument.
  `literalOfConditionalConstraint.ts`.
- nestjs-i18n's `[a, b] = await Promise.all([i18n.t(k), ...])` (13): the await operand had no contextual type
  (getContextualTypeForAwaitOperand), reverse mapped types didn't do arrays/tuples (createReverseMappedType),
  a generic homomorphic mapped context didn't make array literals tuples nor give their elements contextual types
  (getTypeOfPropertyOfContextualType's substituteIndexedMappedType), and fixing went through annotated parameters.
  `awaitContextReverseTuple.ts`.

What the corpus did *not* show: anything about decorators. A clean corpus only finds spurious diagnostics, and
tttt reports none on NestJS's decorator-heavy code; the checks tsc does on decorators (TS1238/TS1241/TS1270...)
are missing checks, which only the suite finds. Decorator checking stays a suite item, not a corpus one.
Note: nestjs-i18n's shipped `.d.ts` imports `Path`/`PathValue`/`IfAnyOrNever` from a module that doesn't export them
(hidden by skipLibCheck), so those are error types; a regression in how tttt treats them would look like an i18n
bug.

## Corpus session of 2026-09-27 (todo entry 7): valibot 49 -> 0, react 1 -> 0, type-fest 47 -> 19
Three sub-agents took valibot's clusters in worktrees while the session did type-fest; everything merged into
ai-main (the commits say which). Probes in `test/probes/plan19/{valibot,typefest}/`. By tsc rule:
- Relations: unconstrained type parameters relate as `unknown` (`{}` only without strictNullChecks, the pre-4.8 rule
  was still here); `{}` isn't a strict subtype of `object`; tuples relate structurally (with their "0"...)
  to non-reference targets; `{ [P in K]: E }[X]` falls back to `{...}[constraint of X]`; conditional types relate
  through their inferred true branch (`Awaited<T>` to `T`); an intersection target's whole-object check fails on
  missing properties; identity: optional properties with undefined added, a rest tuple's required elements count as
  min arguments.
- Conditional types: R15 done, tsc's permissive instantiation (TY_WILDCARD) picks the false branch before the
  restrictive one picks the true one; only aliased conditional types count as tail-recursion steps (IntRange).
- Mapped types: over `any` with an array-constrained T an array; over `[]` the empty tuple; `?` index signatures hold
  undefined; index signatures keep the modifiers type's readonly; readonly of intersection properties only if all
  are; generic mapped types get members from the lower bound of their key type (Omit<T, K>).
- Indexing: past a rest element the union of the rest (R21 array-slice); intersections merge their members' index
  infos; `-0` is `0`; `keyof` a primitive; `keyof typeof Enum` without the reverse mapping; indexing with never.
- Tuples: rest elements keep only the element type of a readonly array they spread (R21 writable-deep); normalization
  merges array spreads and optional elements after a rest; contextual types of array literal elements against tuples
  with rest elements (tsc's getContextualTypeForElementExpression); tuple-like contexts by their "0" property.
- Calls: arity rechecked for candidates instantiated from a generic rest type (expect-type's
  `toEqualTypeOf<X>()`: before this, **every expect-type mismatch in tttt was silent**; it surfaced 52 hidden identity
  divergences in valibot, all fixed the same day); TS2536 for generic element access; async functions' contextual
  return types; expandos of an annotated const; TS2638 (`in` on a `{}` from narrowing unknown, tsc's
  unknownEmptyObjectType).

### What is left
- **R20** (conditional-pick-deep 6): `test/probes/plan29/cpd_steps.ts` was truncated and is restored; standalone it
  agrees with tsc (bar alias display), so the corpus failure depends on what was evaluated first. Reduce with the
  corpus' order kept (tsc runs directly on multi-file probes: `node thirdparty/tsc/lib/tsc.js --noEmit ...`).
- **abstract-class** (4): mixin classes, `agents/plans/32_mixin_classes.md`.
- **jsonify:57**: exactOptionalPropertyTypes' missing type (index signatures ignore it; tttt has none).
- Now that expect-type mismatches are visible, a regression in identity shows up in valibot's test-d files: treat a
  jump there as real.

### Tooling notes
- Multi-file probes against a corpus: put them in `/tmp/x/` with relative imports into `thirdparty/corpora/...`, run
  `./release/tttt -q <options>` and `node thirdparty/tsc/lib/tsc.js --noEmit <options>` (filetest.sh copies the file
  away, so relative imports break).
- `/tmp/realworld.txt` is shared: parallel agents overwrite each other's; copy it right after a run.

## webpack corpus (plan 33, 2026-09-27): 6146 -> 3981, the rest is plan 26 stage 3
`./realworld.py --corpus webpack`: webpack at 45a1bab (May 2026, the last commit on TypeScript 5.9), checked as its
CI does (`tsc` with its tsconfig: allowJs + checkJs, strict, lib es2017, CommonJS; lib/, tooling/, setup/, bin/,
examples' configs; 1238 files / 377k lines with dependencies). tsc 5.8 is clean with two pins in ensure-corpora.sh:
watchpack 2.5.1 (yarn.lock's; 2.5.2's types fail tsc) and `node_modules/webpack -> ..` (the examples
`require("webpack")`, which upstream gets from `yarn link`). tsgo 7 reports 3 TS1003 on JSDoc comments it parses
differently. Fixed the same day (probes in `test/probes/plan33/`):
- A stack overflow: `@param {Constructor} Constructor` where Constructor is an (unsupported) `@typedef` resolved the
  type name to the parameter itself and recursed; now TS2502 like tsc (`jsdoc_values_in_flight`).
- `const X = require("./x")` got esModuleInterop's synthetic default (`{ default: typeof X }`): 2000 TS2351/2349/2507.
- `/// <reference lib>` in a program file (@types/node: es2020, esnext.disposable, esnext.float16) was ignored: ~150
  TS2583/TS2304/TS2550 (the harness still ignores them, TODO in `program_handle_statement`).
- TS2661 for `export { type X }` of a `global {}` name inside a script's `declare module`, TS1095 for a JSDoc
  `@returns` on a setter, TS2370 for `...args: Partial<TArgs>` (getResolvedApparentTypeOfMappedType).

What's left, by root cause (details and order in plan 26 stage 3): unresolved `@typedef` names are silently any, so
every typed hook callback parameter is implicitly any (TS7006 2689, TS7031 375, and the keyof-any/TS2731/TS2502/
TS2694/TS2749 tail); `@type` on `this.x =` and on object literal properties is ignored (TS2345 ~250 on tapable's
constructor, TS2554 127 on hook arities, TS18046 104, TS2339 on `never`, TS2322 `unknown`, TS2488); `@template`
546 sites. TS2578 33 (unused `@ts-expect-error`) are missing diagnostics behind the same gaps. No TypeScript-side
cause is left that I could find; re-rank after each stage 3 piece.

