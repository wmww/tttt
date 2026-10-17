// @strict: true
// @noUnusedLocals: true
// What a deferred function body (plan 78: parsed and walked when a checker first needs it) must give exactly as the
// eager parse and walk: run with baseline_tests --defer-bodies
// @filename: a.ts
// An inferred return type another file asks for first
export const later = (x: number) => { if (x > 1) return "big"; return x; };
export class Counter {
    count = 0;
    constructor(start: number) { this.count = start; }
    // A method whose body another file's call infers from
    next() { return ++this.count; }
}
// A function type in an arrow function's parameters: its walk asks whether the arrow function is context sensitive
export const apply = (f: (x: number) => number, y: number) => { return f(y); };
// An immediately invoked function is part of the flow around it
let assigned: string | undefined;
(() => { assigned = "yes"; })();
export const length: number = assigned.length;
// Expandos of a function in a body
export function withExpandos() {
    function inner() {}
    inner.tag = "t";
    return inner;
}
// Directives inside a body
export function directive() {
    // @ts-expect-error
    const n: number = "no";
    // @ts-expect-error
    const fine: number = 1;
    return n + fine;
}
export function unused() {
    const notRead = 1;
}
// @filename: b.ts
import { later, Counter, apply, withExpandos } from "./a";
const big: string | number = later(2);
const n: number = new Counter(1).next();
const applied: string = apply(x => x * 2, 1);
const tag: number = withExpandos().tag;
export { big, n, applied, tag };
