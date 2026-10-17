// @strict: true
// @lib: es2020
// A tuple in an intersection sees the intersection as its `this`, like any reference (tsc's tuple target has a
// this-type): `sort()` of `[1, 2] & { tag: string }` returns the intersection. Relating such intersections compares
// the target's combined properties with that `this` on both sides (vueuse's makeDestructurable cast)

type X = [1, 2] & { tag: string };
declare let x: X;
const sorted = x.sort();

declare function pair<T extends Record<string, unknown>, A extends readonly any[]>(obj: T, arr: A): T & A;
type D = { d: string };
type R = { r: number };
type Pair = [D, R] & { define: D; reuse: R };
declare const d: D;
declare const r: R;
const p = pair({ define: d, reuse: r }, [d, r]) as Pair;
