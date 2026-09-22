// @target: es2020
// @strict: true
declare const s: Set<string>;
declare const m: Map<string, number>;
declare function arrayFrom<T>(it: Iterable<T>): T[];
for (const x of s) { x; }
for (const [k, v] of m) { k; v; }
const a = arrayFrom(m.keys());
const b = [...s];
const [first, second] = s;
const it = s[Symbol.iterator]();
const sym = Symbol.iterator;
