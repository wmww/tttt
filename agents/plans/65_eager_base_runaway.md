# Plan 65: tsgo's eager-base instantiation runaway (-TS5115, 13 source sweep projects)

The biggest single bucket of the source sweep: tsgo reports TS5115 (or TS5114/TS2589) where tttt reports nothing.
zod 4's `classic/schemas.d.cts:804` (crawlee x6, unocss x2, knip fixture, better-auth i18n/electron and its main
package's `db/to-zod.ts:40`) and postcss-selector-parser through vitepress `postcss/isolateStyles.ts:15` (x2).
Status (2026-10-14, agent A): mechanism understood and prototyped; the prototype fixes all 13 projects but makes the
zod corpus report 20 spurious TS5115/TS5114/TS2589, so it is not committed. What's left is making tttt's laziness
match tsgo's where the cycle passes.

## What tsgo does (instrumented build: a patched `checker.go` in a `go build -overlay` copy, see memory/README)
1. `resolveTypeReferenceMembers(R)` -> `resolveObjectTypeMembers` instantiates R's target's **base types** with a
   fresh mapper (`instantiateType(baseType, mapper)`), eagerly. Instantiating a base reference instantiates its type
   arguments; a conditional type argument is resolved right there (`getConditionalTypeInstantiation` ->
   `getConditionalType` -> `isTypeAssignableTo`).
2. That relation can need R's members again (`getUnmatchedProperty`, `isWeakType`, property types).
   `resolveStructuredTypeMembers` has **no in-flight guard**: R's members are resolved again, with a new mapper, so no
   instantiation cache hits; the conditional instantiation cache (`root.instantiations`) and the relation caches are
   only written when a computation finishes. Every round pushes the same entries on `instantiationStack` until it is
   100 deep: TS5115 with the types three times on the stack (`getCircularTypeNames`), at `currentNode`.
3. The cycle stops early only if it passes something guarded by `pushTypeResolution` on an entity shared between
   rounds. Seen in zod's own corpus (`recursive-types.test.ts`, clean under tsc): `$ZodDefaultInternals<RecursiveA>`
   is resolved re-entrantly exactly once; the second round fails `pushTypeResolution(K, ResolvedBaseConstraint)` (a
   mapped type's type parameter, reached from `getConditionalType` -> `isDeferredType` -> `isGenericMappedType` ->
   `getConstraintOfTypeParameter`) and finishes.

Reductions (tsgo -> TS5115 'Container', 'Diff'):
```ts
type Diff<T, U> = T extends U ? never : T;
interface Base<P extends Container | undefined = Container | undefined> { parent: P; }
interface Container<C extends Node = Node> { first: C; }
interface Root extends Container<Root> { type: "root" }
interface _Selector<S> extends Container<Diff<Node, S>> { type: "selector" }
type Selector = _Selector<Selector>;
export type Node = Root | Selector;
declare const s: Selector;
export const x = s.type; // TS5115 here
```
postcss-selector-parser's real shape has `interface Root extends Container<Selector>` instead of the `s.type` access:
there the cycle starts in `checkTypeArgumentConstraints` (Selector against `C extends Node`) because tsgo's
`getNormalizedType` turns the deferred reference `Selector` into a fresh non-deferred `_Selector<Selector>`, so the
union target doesn't contain it by identity and the relation goes structural. tttt's references are interned (the
deferred one is the same type), so that variant also needs a decision on normalization (probably: skip it, the
project shapes all reach the members some other way too: check vitepress after the main fix).

## Prototype (agent A, not committed)
- `MembersRare.eager_bases`: in `resolve_declared_members()` (generic classes/interfaces only), the bases (with the
  this-argument) whose instantiation resolves a conditional type: a conditional among the type arguments, through
  unions/intersections/non-deferred references (depth 8). Only types that have such bases pay anything.
- `resolve_members_with_this()` / the TY_REFERENCE case of `resolve_members_inner()`: after `instantiate_members()`,
  instantiate the eager bases with the same mapper (results dropped: tttt's members don't need them), with the
  reference, its members and the instantiation depth pushed on a small checker stack.
- `ty_members()`, IN_FLIGHT on a reference that is on that stack: report the runaway and return the members computed
  before the eager bases (returning `empty_members` made `Diff<Root, Selector>` never, a spurious TS2345 in vitepress;
  tsgo's error types only poison the base-derived members, which tttt doesn't take from the bases).
- The report: the stack tsgo has when it stops is tttt's `instantiation_stack` now, then the entries from the eager
  start repeated up to 100; run `getCircularTypeNames` over that (refactor `ty_instantiation_limit_reached()` into a
  function over a given stack). With the tail-call fix (a2e6b9ee) the names match tsgo exactly on zod
  ('IsOptionalIn', '$ZodTypeInternals', '$InferZodRecordInput') and the probe.

Results: the 12 sweep projects and the probe match tsgo; suite unchanged. zod corpus: 20 spurious, two kinds:
1. Re-entrances that tsgo stops at the base-constraint guard (3. above). tttt's cycle reaches the members through
   mapped type instantiation and `as`-clause key filtering (`{...}[k] extends OptionalOutSchema ? never : k` on
   tttt's stack) rather than through a base constraint, so tttt's own resolving stack has nothing to find. A rule
   "only report when the cycle's instantiation stack entries are all references/unions/intersections/conditionals/
   indexed accesses" (no mapped type or keyof: those make tsgo resolve a guarded constraint) removed these.
2. With that rule, still TS2589/TS5114/TS5115 at recursive-types.test.ts:352/353 from the 5M instantiation **count**
   limit (stack only 4-15 deep: `MismatchArgs<StrictEqual...>` -> `keyof T` -> the zod shape's key filter ->
   `$ZodTypeInternals<MakeReadonly<output<T>>, ...>` -> ...): the eager bases of each new
   `ZodOptional<ZodReadonly<ZodObject<{... ZodOptional<ZodReadonly<...>>}>>>` reference resolve `output<T>`, whose
   mapped types tttt expands (keys and `as` clauses) where tsgo's instantiated mapped types stay lazy until their
   members are asked for, so each new reference's bases create the next: unbounded.

## Next steps
1. Find where tttt resolves an instantiated mapped type's keys at instantiation (or `keyof` over one) when tsgo
   doesn't (`expand_mapped`, `ty_mapped_info_instantiate`, the `keyof` of a mapped type with an `as` clause:
   tsgo's `getIndexTypeForMappedType` runs only when asked for the index type). Compare instantiation counts
   (`tsc --extendedDiagnostics` vs `tttt --time`) on a project with only zod's recursive-types.test.ts
   (tsconfig extending zod's with `"types": []`).
2. If that laziness can match tsgo's, re-apply the prototype (with the guard rule of 1., documented as an
   approximation of tsgo's pushTypeResolution guards, or better, by having tttt push RESOLVING_BASE_CONSTRAINT where
   tsgo's isGenericMappedType resolves the constraint) and check zod corpus, flagflip zod/xstate/effect cells and the
   sweeps. Perf: the eager instantiation only runs for references of types with eager bases; measure types and
   instructions on zod, effect, valibot (zod-heavy projects are where it runs).
3. If not, close this as a tsgo artifact: it's a tsgo runaway on declaration files that tsgo users see as an error
   they can't fix; matching it is fidelity, not correctness.

Validation: `scripts/sourcesweep.py -f '^(better-auth|crawlee|knip/packages/knip/fixtures/dependencies/session|unocss/examples/astro|vitepress)'`
matches on the 13 projects, zod/zod-root corpora stay 0/0, `scripts/flagflip.py -c zod` unchanged, the probe above
and a cut-down zod record shape in test/cases.
