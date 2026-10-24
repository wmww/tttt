// Array literals nested in a const-asserted argument are array literal candidates, the empty one too (its tuple is its
// own target): several of them are one candidate, their union (tsc's unionObjectAndArrayLiteralCandidates)
// @strict: true
// @target: es2023
declare function f<V>(x: Iterable<readonly [string, V]>): V;
const r1 = f([['a', ['x']], ['b', []]] as const);
declare function g<V>(x: readonly (readonly [string, V])[]): V;
const r2 = g([['a', ['x']], ['b', []]] as const);
declare function h<V>(a: V, b: V): V;
declare const t1: readonly ["x"];
declare const t0: readonly [];
const r3 = h(t1, t0);
const r4 = h(t0, t1);
const r5 = g([["a", ["x"]], ["b", []]]);
const m: ReadonlyMap<string, readonly string[]> = new Map([["a", ["--x"]], ["b", []]] as const);
