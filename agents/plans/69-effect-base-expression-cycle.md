# Plan 69: effect's 3 spurious TS2578 (a class's base expression that needs the class)

Written 2026-10-14 (agent D). Both halves were implemented and measured, and together they take effect to 0 / 0.
Part (a) costs 1.4% of valibot's instructions, so neither half is committed. The work left is making (a) cheap.

## The divergence
effect's dtslint Schema/TaggedClass.tst.ts:6, TaggedError.tst.ts:31 and TaggedRequest.tst.ts:28 have `// @ts-expect-error`
over `class _A extends Schema.TaggedClass<_A>()("A", { id: Schema.Number }, [undefined, undefined, { pretty: () => (x) =>
... }]) {}`. tsc reports TS2310 + TS2506 there, so the directive is used. tttt reports nothing, so it says TS2578.
Reduction (tsgo: TS2310 + TS2506 at `_A`; tttt: nothing):
```ts
// @target: es2022
// @strict: true
interface Ann<A> { pretty?: () => (x: A) => string }
type Annots<Self, A> = readonly [Ann<Self> | undefined, Ann<Self>?, Ann<A>?];
declare function TaggedClass<Self>(): <F extends object>(tag: string, fields: F, annotations?: Annots<Self, F>) => new () => F & { _tag: string };
class _A extends TaggedClass<_A>()("A", { id: 1 }, [undefined, undefined, { pretty: () => (x) => "" }]) {}
```
With `annotations?: Ann<Self> | Ann<F>` and a plain object argument (no tuple), tsgo reports TS2310 + TS2506 + TS2577
(at the `() => (x: A) => string` return type). tttt reports only TS2310 there.

## Mechanism (tsgo)
1. getBaseConstructorTypeOfClass runs under pushTypeResolution(class, ResolvedBaseConstructorType) and checks the call.
2. Relating the argument tuple to `readonly [Ann<Self> | undefined, Ann<Self>?, Ann<A>?]` takes two references with
   different tuple targets. That goes structurally: propertiesRelatedTo (element by element), then
   **indexSignaturesRelatedTo**. Its number index is `undefined | { pretty }` against the union of the target's
   elements, where `Ann<_A>` is tried first. That relates `_A` structurally and resolves _A's base types, which need
   the base constructor type. The cycle marks both entries: TS2506 (base constructor errorType) and TS2310.
tttt relates tuples element by element only, so `{ pretty }` only meets `Ann<F>`. tttt also has no base constructor
resolution entry.

## The two halves as implemented (and reverted)
(b) **The base constructor resolution entry.** It is cheap: no instruction change.
- ty.h: add `RESOLVING_BASE_CONSTRUCTOR` (ClassDecl key) and `SymbolLinks.base_constructor_circular`.
- members.c class_base(), in the "any other expression" path: return false if the flag is set. Otherwise push the
  entry around `ty_expr_type(me, extends)`, and if the pop reports a cycle, set the flag and return false.
- The bases pop (`if (ty_resolving_pop(me))` in the bases resolution): when the class's base_constructor_circular is
  set, add BASE_CYCLE_REENTERED (TS2310 still fires) instead of KNOWN|IN_CYCLE.
- class_decl.c, before the `ty_has_base_type` TS2506: report TS2506 when ty_class_base_constructor_circular().
(a) **The tuple-to-tuple number index relation**, in relate.c's `source_is_tuple && target_is_tuple` branch after
tuples_related():
```c
if (elements && st->relation != TY_RELATION_IDENTITY && is_tuple(source) && is_tuple(target) &&
    source->reference.target != target->reference.target) {
    Ty const* number = ty_intrinsic(me, TY_NUMBER);
    IndexInfo const* ti = ty_applicable_index_info(me, apparent_type(me, target), number);
    IndexInfo const* si = ti ? ty_applicable_index_info(me, apparent_type(me, source), number) : nullptr;
    if (si) elements &= is_related_to(me, st, si->value, ti->value, false);
}
```

## Numbers (callgrind, against ai-main 65aa0d23)
| | effect | valibot | vue | tsc |
|---|---|---|---|---|
| base | 10.211 G | 3.456 G | 6.593 G | 4.694 G |
| (a)+(b) | 10.220 G | 3.505 G (+1.4%) | 6.593 G | 4.696 G |

Results with (a)+(b): suite unchanged (13585), every corpus unchanged except effect 3 -> 0 spurious, DT sweep 8594,
`$ExpectType` 68637.

What was tried against valibot's cost, all without enough effect:
- Gating on the target's elements having two or more object types: still +1.4%.
- Also requiring an unwidened object literal among the source's elements: +0.7%, and it's a heuristic.
- With the index relation disabled: base numbers, so the cost is in that relation (unions of schema objects relating
  pairwise), not in finding the gate.

## Next step
Find why relating the two index unions is expensive in valibot (`TTTT_DEBUG_RELATE_NESTED`). Likely candidates: fresh
literal sources that miss the relation cache, or each element's union being related again although the elements just
were. Then either cache that relation or find an exact condition under which it is implied. tsgo's relation cache keys
on the pair, so it gets the repeat for free. Validation: effect 0 / 0, valibot within 0.3%, and the reduction above
added as a test (the plain-object variant also needs TS2577 at the return type, which tttt lacks; leave that one out).
