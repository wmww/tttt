// @target: es2020
// @strict: true
const sym = Symbol();
let wide = Symbol();
declare const decl: unique symbol;
class C {
    static readonly key: unique symbol = Symbol("key");
    [sym]: number = 1;
    [Symbol.iterator]() { return [1, 2][Symbol.iterator](); }
}
const o = { [sym]: "a", [C.key]: true };
type K = keyof typeof o;
const v = o[sym];
declare const c: C;
for (const x of c) { x; }
