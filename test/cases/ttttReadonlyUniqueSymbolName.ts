// @target: es2015
// Readonly-assignment messages name a unique symbol property as tsc writes it, `[a]`, not by its internal key
const a = Symbol();
class C { get [a]() { return ""; } }
declare const c: C;
c[a] = "x";
delete c[a];
const o = { [a]: 1 } as const;
o[a] = 2;
interface I { readonly [a]: number }
declare const i: I;
i[a] = 3;
const s: { [a]: string } = {};
