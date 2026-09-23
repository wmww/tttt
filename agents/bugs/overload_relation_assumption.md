# Overloaded methods lose the in-progress relation assumption

```ts
interface A<T> { reduce(cb: (p: number, array: A<T>) => number): number;
                 reduce(cb: (p: number, array: A<T>) => number, init: number): number;
                 readonly [Symbol.toStringTag]: "a" }
// B<T>: the same with "b"
a = b; // tsc blames [Symbol.toStringTag]; tttt blames `reduce`
```
With one `reduce` overload tttt is right, with two it isn't: somewhere in the N x M signature comparison of
overloaded members (erasure of generic signatures? caching a result that depended on a "maybe" from the in-progress
`B -> A` pair?) the assumption that `B<T> -> A<T>` holds while it's being checked is lost, and the callback's
`array: A<T>` parameter then fails.

Affected: typedArraysCrossAssignability01 (lost when plan 10 made `array: this` resolve: the lib's typed arrays have
exactly this shape), complexRecursiveCollections (6 spurious TS2430 when the heritage check is on: `Seq<K, M>` vs
`Collection<K, M>` with an overloaded generic `filter`).

Start with a probe of the snippet above and `TTTT_DEBUG_RELATE=1`; look at how `relate.c` caches results computed
under a maybe-assumption (tsc only caches those once the outermost relation succeeds: `maybeKeys` in
`recursiveTypeRelatedTo`).
