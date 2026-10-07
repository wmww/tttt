// @strict: true
// @noUncheckedIndexedAccess: true
class C { c = 1 }
type X = { [k: string]: C }[symbol];
declare const o: { [k: string]: C };
declare const sym: symbol;
declare const us: unique symbol;
const a = o[sym];
const a2 = o[us];
declare const ss: string | symbol;
const a3 = o[ss];
o[sym] = new C();
function f<T extends { [k: string]: C }>(t: T) { return t[sym]; }
declare const z: { [k: string]: C; [k: symbol]: number };
const a4 = z[sym];
declare const o: { [k: string]: number };
declare const sym: symbol;
const { [sym]: x } = o;
declare const r: Record<string, number>;
r[sym];
declare const n: { [k: string]: number } | undefined;
n?.[sym];
declare const p: { [k: string]: number; a: string } & { b: number };
p[sym];
function g(input: Iterable<number> | Record<string, number>) {
    if (Symbol.iterator in input) { for (const e of input) {} }
}
