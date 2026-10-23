// @noTypesAndSymbols: true
// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "target": "es2022", "lib": ["es2022"] } }
// @filename: a0.ts
interface Item { next: Item | null }
declare const more: boolean; declare const item: Item;
function base() {
  let last: Item | null = null;
  while (more) {
    if (last === null) last = item;
    else last = last.next = item;
  }
}
const sentinel: number = "";
export {};
// @filename: a1.ts
interface Item { next: Item | null; n: number; s: string; arr: Item[]; cb: (x: Item) => Item; opt?: Item }
declare const more: boolean; declare const item: Item; declare const items: Item[];
declare function id<T>(x: T): T;
declare function two<T>(a: T, b: T): T;
function a01() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.next = item); } }
function a02() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = last["next"] = item; } }
function a03() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.n = 1, item); } }
function a04() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = more ? (last.next = item) : item; } }
function a05() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = id(last.next = item); } }
function a06() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = [last.next = item][0]; } }
function a07() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = { v: last.next = item }.v; } }
function a08() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.next ??= item); } }
function a09() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.next ||= item); } }
function a10() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.next &&= item) ?? item; } }
function a11() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.n += 1, item); } }
function a12() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.s += "a", item); } }
function a13() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.n++, item); } }
function a14() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (delete last.opt, item); } }
function a15() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = ([last.next] = [item], item); } }
function a16() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = ({ a: last.next } = { a: item }, item); } }
function a17() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = ([last.n = 5] = [1], item); } }
function a18() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = ({ a: last.next = item } = { a: item }, item); } }
function a19() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = ([...last.arr] = items, item); } }
function a20() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = last.cb(last.next = item); } }
function a21() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.next = item)!; } }
function a22() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.next = item) as Item; } }
function a23() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.next = item) satisfies Item; } }
function a24() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = last.arr[last.n = 1]; } }
function a25() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = two(last.next = item, item); } }
function a26() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (() => last!.next = item)(); } }
function a27() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = `${last.s = "x"}` ? item : item; } }
function a28() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = [...(last.arr = items)][0]; } }
function a29() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.next = last.next = item); } }
function a30() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.next = item) && item; } }
function a31() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.arr[0] = item); } }
function a32() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last.arr[0].next = item); } }
function a33() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = ((last).next = item); } }
function a34() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = (last!.next = item); } }
function a35() { let last: Item | null = null;
  while (more) { if (last === null) last = item; else last = typeof (last.next = item) === "object" ? item : item; } }
const sentinel: number = "";
export {};
// @filename: a2.ts
interface Item { next: Item | null; n: number; s: string; arr: Item[] }
interface A { k: "a"; v: string; set(x: string): void } interface B { k: "b"; v: number; b: number; set(x: number): void }
declare const more: boolean; declare const item: Item; declare const items: Item[]; declare const av: A; declare const bv: B;
declare function two<T>(a: T, b: T): T;
declare function isB(x: A | B): x is B;
function b01() { let x: string | number = "a"; while (more) { x = x === 5 ? "b" : 5; } }
function b02() { let s: "a" | "b" = "a"; while (more) { s = s === "b" ? "a" : "b"; } }
function b03() { let x: string | number = "a"; while (more) { x = two(x, 5); } }
function b04() { let x: string | number = "a"; while (more) { x = x !== 5 ? 5 : "b"; } }
function b05() { let o: A | B = av; while (more) { o = o.k === "b" ? av : bv; } }
function b06() { let o: A | B = av; while (more) { o = o.k === "b" ? (o.b, av) : bv; } }
function b07() { let o: A | B = av; while (more) { o = isB(o) ? (o.b, av) : bv; } }
function b08() { let o: A | B = av; while (more) { o = "b" in o ? (o.b, av) : bv; } }
function b09() { let x: string | number = "a"; while (more) { x = typeof x === "number" ? x.toFixed() : 5; } }
function b10() { let x: string | number = "a"; while (more) { x = (x as number) + 1; } }
function b11() { let x: string | number | null = null; while (more) { x = x === null ? 5 : x satisfies number | string; } }
function b12() { let x: string | number = "a"; while (more) { x = x == 5 ? "b" : 5; } }
function b13() { let x: string | number = "a"; while (more) { x = [5].includes(x as never) ? 1 : "a"; } }
function b14() { let x: string | number = "a"; while (more) { x = x < 5 ? 1 : "a"; } }
function b15() { let x: string | boolean = "a"; while (more) { x = x === true ? "b" : true; } }
function b16() { let x: 1 | 2 | 3 = 1; while (more) { x = x === 3 ? 1 : x === 1 ? 2 : 3; } }
function b17() { let x: 1 | 2 | 3 = 1; while (more) { switch (x) { case 3: x = 1; break; case 1: x = 2; break; default: x = 3; } } }
function b18() { let x: 1 | 2 | 3 = 1; while (more) { x = (() => { switch (x) { case 3: return 1; default: return 3; } })(); } }
function b19() { let o: A | B = av; while (more) { o = (o.set(o.v as never), o.k === "a" ? bv : av); } }
function b20() { let x: string | number = "a"; while (more) { x = `${x}` === "5" && x === 5 ? "b" : 5; } }
// loop variants, silentNever target
function c01() { let last: Item | null = null; do { if (last === null) last = item; else last = last.next = item; } while (more); }
function c02() { let last: Item | null = null; for (;;) { if (last === null) last = item; else last = last.next = item; if (more) break; } }
function c03() { let last: Item | null = null; for (const it of items) { if (last === null) last = it; else last = last.next = it; } }
function c04() { let last: Item | null = null; for (const k in item) { if (last === null) last = item; else last = last.next = item; } }
function c05() { let last: Item | null = null; for (let i = 0; i < 3; i++) { if (last === null) last = item; else last = last.next = item; } }
function c06() { let last: Item | null = null; for (; more; last = last === null ? item : (last.next = item)) { } }
function c07() { let last: Item | null = null; outer: while (more) { while (more) { if (last === null) { last = item; continue outer; } last = last.next = item; } } }
function c08() { let last: Item | null = null; while (more) { while (more) { if (last === null) last = item; else last = last.next = item; } } }
function c09() { let last: Item | null = null; while (more) { switch (last) { case null: last = item; break; default: last = last.next = item; } } }
function c10() { let last: Item | null = null; while (more) { if (!last) last = item; else last = last.next = item; } }
function c11() { let last: Item | undefined; while (more) { if (last === undefined) last = item; else last = last.next = item; } }
function c12() { let last: Item | undefined; while (more) { if (last) last = last.next = item; else last = item; } }
function c13() { let last: Item | null = null; while (more) { last = last === null ? item : (last.next = item); } }
function c14() { let last: Item | null = null; while (more) { last = last && (last.next = item); last = item; } }
function c15() { let last: Item | null = null; while (more) { if (last !== null) { last = last.next = item; continue; } last = item; } }
function c16() { let last: Item | null = null; while (more) { try { if (last === null) last = item; else last = last.next = item; } finally { } } }
function c17() { let last: Item | null = null; while (more) { if (last === null) last = item; else { const t = last.next = item; last = t; } } }
function c18() { let last: Item | null = null; while (more) { if (last === null) last = item; else { const t: Item = last.next = item; last = t; } } }
function c19() { let last: Item | null = null; while (more) { if (last === null) last = item; else { last.next = item; last = last.next; } } }
function c20() { let last: Item | null = null; let prev: Item | null = null; while (more) { if (last === null) last = item; else { prev = last; last = prev.next = item; } } }
function c21() { let last: Item | null = null; let prev: Item | null = null; while (more) { if (prev === null) prev = item; else { last = prev.next = item; prev = last; } } }
const sentinel: number = "";
export {};
// @filename: a3.ts
declare const more: boolean; declare function str(x: unknown): string;
function d01() { let x: string | number = "a"; while (more) { x = more && x === 5 ? "b" : 5; } }
function d02() { let x: string | number = "a"; while (more) { x = x === 5 && more ? "b" : 5; } }
function d03() { let x: string | number = "a"; while (more) { x = more || x === 5 ? "b" : 5; } }
function d04() { let x: string | number = "a"; while (more) { x = `${x}` === "5" ? "b" : 5; } }
function d05() { let x: string | number = "a"; while (more) { x = str(x) === "5" && x === 5 ? "b" : 5; } }
function d06() { let x: string | number = "a"; while (more) { x = (x, x === 5) ? "b" : 5; } }
function d07() { let x: string | number = "a"; while (more) { x = [x === 5][0] ? "b" : 5; } }
function d08() { let x: string | number = "a"; while (more) { x = str(x === 5) ? "b" : 5; } }
function d09() { let x: string | number = "a"; while (more) { x = { p: x === 5 }.p ? "b" : 5; } }
function d10() { let x: string | number = "a"; while (more) { x = !(x === 5) ? "b" : 5; } }
function d11() { let x: string | number = "a"; while (more) { x = (x === 5)! ? "b" : 5; } }
function d12() { let x: string | number = "a"; while (more) { x = `${x === 5}` ? "b" : 5; } }
function d13() { let x: string | number = "a"; while (more) { x = (x === 5 ? 1 : 2) === 1 ? "b" : 5; } }
function d14() { let x: string | number = "a"; while (more) { x = x === 5 || x === "q" ? "b" : 5; } }
function d15() { let x: string | number = "a"; while (more) { x = typeof (x === 5) === "boolean" ? "b" : 5; } }
function d16() { let x: string | number = "a"; while (more) { x = (x === 5) as boolean ? "b" : 5; } }
function d17() { let x: string | number = "a"; while (more) { x = (x === 5) satisfies boolean ? "b" : 5; } }
function d18() { let x: string | number = "a"; while (more) { x = more ? (x === 5 ? "b" : 5) : 5; } }
function d19() { let x: string | number = "a"; while (more) { x = more && x === 5 ? "b" : 5; } }
function d20() { let x: string | number = "a"; while (more) { x = (more && x === 5) ? "b" : 5; } }
function d21() { let x: string | number = "a"; while (more) { x = (x === 5 ? "b" : 5); } }
function d22() { let x: string | number = "a"; while (more) { x = more ? "b" : x === 5 ? 1 : 2; } }
function d23() { let x: string | number = "a"; while (more) { x = (more ?? x === 5) ? "b" : 5; } }
function d24() { let x: string | number = "a"; while (more) { x = +(x === 5) ; } }
function d25() { let x: string | number = "a"; while (more) { x = (void (x === 5), 5); } }
function d26() { let x: string | number = "a"; while (more) { x = [...[x === 5]].length; } }
function d27() { let x: string | number = "a"; while (more) { x = (() => x === 5)() ? 1 : "a"; } }
function d28() { let x: string | number = "a"; while (more) { x = { get p() { return x === 5; } }.p ? 1 : "a"; } }
function d29() { let x: string | number = "a"; let y = false; while (more) { x = (y = x === 5) ? 1 : "a"; } }
function d30() { let x: string | number = "a"; while (more) { x = more ? "b" : 5; if (x === 5) { } } }
const sentinel: number = "";
export {};
// @filename: a4.ts
interface Item { next: Item | null; n: number }
declare const more: boolean; declare const item: Item; declare const fn: () => void; declare const pr: Promise<number>;
class Foo { f = 1 } declare const foo: Foo; declare const sym: symbol;
function e01() { let f: (() => void) | undefined = fn; while (more) { f = f ? undefined : fn; } }
async function e02() { let p: Promise<number> | undefined = pr; while (more) { p = p ? undefined : pr; } }
function e03() { let x: string | number = "a"; while (more) { x = (more && x === 5) && more ? "b" : 5; } }
function e04() { let x: string | number = "a"; while (more) { x = ((more && x === 5) && 5) || "a"; } }
function e05() { let x: string | number = "a"; while (more) { x = ((more || x === 5) && 5) || "a"; } }
function e06() { let x: string | number = "a"; while (more) { x = more && (x === 5 ? 1 : 2) ? "b" : 5; } }
function e07() { let x: string | number = "a"; while (more) { x = (more && more && x === 5) ? "b" : 5; } }
function e08() { let x: string | number = "a"; while (more) { x = (more && (more || x === 5)) ? "b" : 5; } }
function e09() { let x: string | number = "a"; while (more) { x = ((more && x === 5)) ? "b" : 5; } }
function e10() { let x: string | number = "a"; while (more) { x = !(more && x === 5) ? "b" : 5; } }
function e11() { let x: string | number = "a"; while (more) { x = (more && x !== 5) ? "b" : 5; } }
function e12() { let x: string | number = "a"; while (more) { x = (more && x == 5) ? "b" : 5; } }
function e13() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = (more && (last.next = item)) ? item : item; } }
function e14() { let f: (() => void) | undefined = fn; while (more) { f = (more && f) ? undefined : fn; } }
function e15() { let f: (() => void) | undefined = fn; while (more) { f = ((f && more) ? undefined : fn); } }
function e16() { let f: (() => void) | undefined = fn; while (more) { f = (f && more) || more ? undefined : fn; } }
function e17() { let f: (() => void) | undefined = fn; while (more) { f = (more || f) && more ? undefined : fn; } }
function e18() { let x: string | Foo = "a"; while (more) { x = x instanceof Foo ? "b" : foo; } }
function e19() { let x: string | symbol = "a"; while (more) { x = `${x}` ? sym : "a"; } }
function e20() { let x: number | Foo = 1; while (more) { x = { ...x }.f ? 1 : foo; } }
function e21() { let x: string | number = "a"; while (more) { x = [x].includes(5) ? "a" : 5; } }
function e22() { let x: string | number = "a"; while (more) { x = more ? (more && x === 5 ? "b" : 5) : 5; } }
function e23() { let x: string | number = "a"; while (more) { x = [more && x === 5 ? "b" : 5][0]; } }
function e24() { let x: string | number = "a"; let y = 0; while (more) { x = (y = more && x === 5 ? 1 : 2) ? "b" : 5; } }
function e25() { let x: string | number = "a"; while (more) { x = (more && x === 5 ? "b" : 5) as string | number; } }
function e26() { let x: string | number = "a"; while (more) { x = { p: more && x === 5 ? "b" : 5 }.p; } }
function e27() { let x: string | number = "a"; while (more) { x = (more, more && x === 5 ? "b" : 5); } }
function e28() { let x: string | number = "a"; do { x = more && x === 5 ? "b" : 5; } while (more); }
function e29() { let x: string | number = "a"; for (;;) { x = more && x === 5 ? "b" : 5; } }
function e30() { let x: string | number = "a"; for (const _ of [1]) { x = more && x === 5 ? "b" : 5; } }
function e31() { let x: string | number = "a"; while (more) { const y = more && x === 5 ? "b" : 5; x = y; } }
function e32() { let x: string | number = "a"; while (more) { let y: string | number; y = more && x === 5 ? "b" : 5; x = y; } }
function e33() { let x; x = "a"; while (more) { x = more && x === 5 ? "b" : 5; } }
function e34() { let x: string | number = "a"; while (more) { x = more && x === 5 ? "b" : 5; x = "a"; } }
function e35() { let x: string | number = "a"; while (more) { if (more) continue; x = more && x === 5 ? "b" : 5; } }
function e36() { let x: string | number = "a"; while (more) { while (more) { x = more && x === 5 ? "b" : 5; } } }
function e37() { let x: string | number = "a"; let z: string | number = "a"; while (more) { z = more && x === 5 ? "b" : 5; x = z; } }
const sentinel: number = "";
export {};
// @filename: a5.ts
interface Item { next: Item | null; n: number }
interface A { k: "a"; v: string } interface B { k: "b"; v: number; b: number }
interface A2 { k: 1; v: 1; extra: 1 } interface B2 { k: 2; v: 2 }
type OnlyKV<K extends "k" | "v"> = K;
declare const more: boolean; declare const item: Item; declare const fn: () => void; declare const av: A; declare const bv: B; declare const a2: A2; declare const b2: B2;
declare function takeNum(n: number): number; declare function id<T>(x: T): T;
declare let numVar: number; declare const objN: { n: number }; declare const arrN: number[]; declare let numU: number | undefined;
function g01() { let f: string | (() => void) = "a"; while (more) { f = more ? fn : (f(), "a"); } }
function g02() { let o: A | B = av; while (more) { o = more ? bv : (o.b, av); } }
function g03() { let x: string | number = "a"; while (more) { x = more ? "a" : takeNum(x); } }
function g04() { let x: string | number = "a"; while (more) { x = more ? "a" : (numVar = x, 5); } }
function g05() { let x: string | number = "a"; while (more) { x = more ? "a" : (numVar -= x, 5); } }
function g06() { let x: string | number = "a"; while (more) { x = more ? "a" : ([numVar] = [x], 5); } }
function g07() { let x: string | number = "a"; while (more) { x = more ? "a" : (x.toFixed(), 5); } }
function g08() { let x: string | number = "a"; while (more) { x = more ? "a" : (x satisfies number, 5); } }
function* g09(): Generator<number, void, unknown> { let x: string | number = "a"; while (more) { x = more ? "a" : (yield x, 5); } }
function g10() { let x: string | number = "a"; while (more) { x = more ? "a" : (objN.n = x, 5); } }
function g11() { let x: string | number = "a"; while (more) { x = more ? "a" : (arrN[0] = x, 5); } }
function g12() { let x: string | number = "a"; while (more) { x = more ? "a" : (numU ??= x, 5); } }
function g13() { let x: string | number = "a"; while (more) { x = more ? "a" : ({ n: numVar } = { n: x }, 5); } }
function g14() { let x: string | number = "a"; while (more) { x = more ? "a" : ([numVar = x] = [], 5); } }
function g15() { let x: string | number = "a"; while (more) { x = more ? "a" : takeNum(...[x] as const); } }
function g16() { let x: string | number = "a"; while (more) { x = more ? "a" : (numVar = numVar = x, 5); } }
function g17() { let x: string | number = "a"; while (more) { x = more ? "a" : (numVar += x, 5); } }
function g18() { let x: string | number = "a"; while (more) { x = more ? "a" : ({ n: numVar = 1 } = { n: x }, 5); } }
function g19() { let x: string | number = "a"; while (more) { x = more ? "a" : ([objN.n] = [x], 5); } }
function g20() { let x: string | number = "a"; while (more) { x = more ? "a" : ([...arrN] = [x], 5); } }
function g21() { let x: string | number = "a"; while (more) { x = more ? "a" : (x as boolean, 5); } }
function g22() { let x: string | number = "a"; while (more) { x = more ? "a" : (<boolean>x, 5); } }
function g23() { let x: string | number = "a"; while (more) { x = more ? "a" : (-x, 5); } }
function g24() { let x: string | number = "a"; while (more) { x = more ? "a" : (arrN[x], 5); } }
function g25() { let x: string | number = "a"; while (more) { x = more ? "a" : ("k" in x, 5); } }
function g26() { let x: string | number = "a"; while (more) { x = more ? "a" : (new x(), 5); } }
function g27() { let x: string | number = "a"; while (more) { x = more ? "a" : (x`a`, 5); } }
function g28() { let x: string | number = "a"; while (more) { x = more ? "a" : ({ ...x }, 5); } }
function g29() { let x: string | number = "a"; while (more) { x = more ? "a" : ([...x].length); } }
function g30() { let x: string | number = "a"; while (more) { x = more ? "a" : (x * 2); } }
function f01() { let x: string | number = "a"; while (more) { x = more ? "a" : ((a: number = x) => a)(); } }
function f02() { let o: A2 | B2 = a2; while (more) { o = more ? b2 : ((null! as OnlyKV<keyof typeof o>), a2); } }
function f03() { let o: A2 | B2 = a2; while (more) { o = more ? b2 : (id<OnlyKV<keyof typeof o>>(null!), a2); } }
function f04() { let o: A2 | B2 = a2; while (more) { o = more ? b2 : ((null! satisfies OnlyKV<keyof typeof o>), a2); } }
function f05() { let o: A2 | B2 = a2; while (more) { o = more ? b2 : ((id<OnlyKV<keyof typeof o>>), a2); } }
function f06() { let x: string | number = "a"; while (more) { x = more ? "a" : (({ a = x }: { a?: number }) => a)({}); } }
function h02() { let a = []; while (more) { a = a.concat(1); } return a; }
function h03() { let a = []; while (more) { a.push(a.length); } return a; }
function h04() { let a = []; while (more) { a[a.length] = a.length ? "s" : 1; } return a; }
function h05() { let x = null; while (more) { x = x === null ? item : (x.next = item); } }
function h06() { let x; while (more) { if (x === undefined) x = item; else x = x.next = item; } }
function h07() { let a = []; while (more) { a[0] = a.length === "s" ? 1 : 2; } return a; }
function h08() { let x; x = "a"; while (more) { x = more ? 5 : (numVar = x, "a"); } }
function i01() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = { m() { return last!.next = item; } }.m(); } }
function i02() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = new (class { p = (last!.next = item); })().p; } }
async function j01() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = await (last.next = item); } }
function* j02() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = ((yield (last.next = item)), item); } }
const sentinel: number = "";
export {};
// @filename: a6.ts
interface Item { next: Item | null; n: number }
interface A { k: "a"; v: string } interface B { k: "b"; v: number; b: number }
interface A2 { k: 1; v: 1; extra: 1 } interface B2 { k: 2; v: 2 }
type OnlyKV<K extends "k" | "v"> = K; class Box<T extends "k" | "v"> { p!: T }
declare const more: boolean; declare const item: Item; declare const fn: () => void; declare const av: A; declare const bv: B; declare const a2: A2; declare const b2: B2;
declare const Ctor: new () => Item; declare const tag: (s: TemplateStringsArray) => void; declare let numVar: number;
function k01() { let f: (() => void) | undefined = undefined; while (more) { f = more ? fn : (f(), undefined); } }
function k02() { let o: Item | undefined = undefined; while (more) { o = more ? item : (o.n, undefined); } }
function k03() { let o: Item | undefined = undefined; while (more) { o = more ? item : (o.n = 1, undefined); } }
function k04() { let o: Item | null = null; while (more) { o = more ? item : (o!.next = item); } }
function k05() { let o: Item | null = null; while (more) { o = more ? item : o!.next; } }
function k06() { let C: (new () => Item) | undefined = undefined; while (more) { C = more ? Ctor : (new C(), undefined); } }
function k07() { let f: ((s: TemplateStringsArray) => void) | undefined = undefined; while (more) { f = more ? tag : (f`a`, undefined); } }
function k08() { let o: Item | null = null; while (more) { o = more ? item : o!["next"]; } }
function k09() { let o: A | B = av; while (more) { o = more ? bv : (o["b"], av); } }
function k10() { let o: A | B = av; while (more) { o = more ? bv : ({ b: numVar } = o, av); } }
function k11() { let o: A2 | B2 = a2; while (more) { o = more ? b2 : (((p: OnlyKV<keyof typeof o>) => 1)(null!), a2); } }
function k12() { let o: A2 | B2 = a2; while (more) { o = more ? b2 : (new Box<keyof typeof o>(), a2); } }
function k13() { let o: A2 | B2 = a2; while (more) { o = more ? b2 : ((null! as (typeof o)["extra"]), a2); } }
function k14() { let o: Item | null = null; while (more) { o = more ? item : (o!.next!.next = item); } }
function k15() { let o: Item | null = null; while (more) { o = more ? item : o!; } }
function k16() { let o: Item | null = null; while (more) { o = more ? item : (o as Item).next; } }
function k17() { let o: Item | null = null; while (more) { o = more ? item : (numVar = o!.n, item); } }
function k18() { let o: Item | null = null; while (more) { if (o === null) o = item; else o = o!.next = item; } }
function k19() { let o: Item | null | undefined = null; while (more) { o = more ? item : o?.next; } }
function k20() { let o: Item | null = null; let p: Item | null = null; while (more) { p = o; o = more ? item : (p!.next = item); } }
function k21() { let head: Item | null = null, tail: Item | null = null; while (more) { if (!head) head = tail = item; else tail = tail!.next = item; } return head; }
function k22() { let o: Item | null = null; while (more) { o = more ? item : (o!.n++, item); } }
function k23() { let o: Item | null = null; while (more) { o = more ? item : (delete o!.n, item); } }
function k24() { let o: Item | null = null; for (const it of [item]) { o = o ? (o!.next = it) : it; } }
function k25() { let o: Item | null = null; while (more) { o = more ? item : (({ n: o!.n } = item), item); } }
const sentinel: number = "";
export {};
// @filename: a7.ts
interface A2 { k: 1; v: 1; extra: 1 } interface B2 { k: 2; v: 2 }
declare const more: boolean; declare const a2: A2; declare const b2: B2; declare let numVar: number;
declare function takeNum(n: number): number; declare function id<T>(x: T): T;
declare function apply<T>(a: T, cb: (v: T) => number): number;
function m01() { let o: A2 | B2 = a2; while (more) { o = more ? b2 : ((null! as typeof o).extra, a2); } }
function m02() { let x: string | number = "a"; while (more) { x = more ? "a" : ((null! as typeof x) satisfies string, 5); } }
function m03() { let x: string | number = "a"; while (more) { x = more ? "a" : takeNum(null! as typeof x); } }
function m05() { let x: string | number = "a"; while (more) { x = more ? "a" : ((p: typeof x) => 5)(x); } }
function m06() { let x: string | number = "a"; while (more) { x = more ? "a" : (id<typeof x>(x), 5); } }
function m07() { let x: string | number = "a"; while (more) { x = more ? "a" : (id<typeof x>(5), 5); } }
function m08() { let x: string | number = "a"; while (more) { x = more ? "a" : (5 satisfies typeof x, 5); } }
function m09() { let x: string | number = "a"; while (more) { x = more ? "a" : (5 as typeof x, 5); } }
function m10() { let x: string | number = "a"; while (more) { x = more ? "a" : apply(x, v => v.length); } }
function m11() { let x: string | number = "a"; while (more) { x = more ? "a" : apply(x, v => takeNum(v)); } }
function m12() { let x: string | number = "a"; while (more) { x = more ? "a" : [x].map(v => v.length)[0]; } }
function m13() { let x: string | number = "a"; while (more) { x = more ? "a" : (((v = x) => v.length)(), 5); } }
function m14() { let x: string | number = "a"; while (more) { x = more ? "a" : (numVar = (null! as typeof x extends string ? number : string), 5); } }
function m15() { let x: string | number = "a"; while (more) { x = more ? "a" : (numVar = (null! as (typeof x)[]).length, 5); const y: typeof x = 5; } }
function n01() { let a = []; let x: string | number = "a"; while (more) { a.push(x); x = a.length ? 5 : "a"; } const r: string[] = a; }
function n02() { let a = []; let x: string | number = "a"; while (more) { a[0] = x; x = a.length ? 5 : "a"; } const r: string[] = a; }
function n03() { let a = []; let x: string | number = "a"; while (more) { a.push(x === 5); x = a.length ? 5 : "a"; } return a; }
function n04() { let a = []; let x: string | number = "a"; while (more) { a.push(more && x === 5 ? 1 : 2); x = a.length ? 5 : "a"; } return a; }
function n05() { let a = []; let x: string | number = "a"; while (more) { a.push(takeNum(x)); x = a.length ? 5 : "a"; } return a; }
function n06() { let a = []; let x: string | number = "a"; while (more) { a.push(numVar = x); x = a.length ? 5 : "a"; } return a; }
function n07() { let a = []; let x: string | number = "a"; while (more) { a.push([x]); x = a.length ? 5 : "a"; } const r: string[][] = a; }
function n08() { let a = []; let x: string | number = "a"; while (more) { a.unshift(x); x = a[0] === "a" ? 5 : "a"; } const r: string[] = a; }
const sentinel: number = "";
export {};
// @filename: a8.ts
interface Item { next: Item | null; n: number }
declare const more: boolean; declare const item: Item; declare const items: Item[];
declare function ov(a: Item): Item; declare function ov(a: Item, b: number): Item;
declare function gen<T>(a: T, cb: (v: T) => T): T;
declare function ctx(o: { it: Item; f: (i: Item) => Item }): Item;
function r01() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = ov(last.next = item); } }
function r02() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = ov(last.next = item, 1); } }
function r03() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = gen(last.next = item, v => v); } }
function r04() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = ctx({ it: last.next = item, f: i => i }); } }
function r05() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = ([last.next = item] as const)[0]; } }
function r06() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = items.map(i => i)[last.n = 0]; } }
function r07() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = gen(item, v => (last!.next = v)); } }
function r08() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = ov(ov(last.next = item)); } }
function r09() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = more ? ov(last.next = item) : gen(last.next = item, v => v); } }
function r10() { let last: Item | null = null; let other: Item | null = null; while (more) { if (last === null || other === null) { last = item; other = item; } else { last = other.next = item; other = last.next = item; } } }
function r11() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = ((last.next = item), (last.n = "s"), item); } }
function r12() { let last: Item | null = null; while (more) { if (last === null) last = item; else last = ((last.next = item), (item.n = "s"), item); } }
function r13() { let last: Item | null = null; let x: string | number = "a"; while (more) { if (last === null) last = item; else last = ((last.next = item), (item.n = x), item); x = more ? "a" : 5; } }
function r14() { let last: Item | null = null; let x: string | number = "a"; while (more) { if (last === null) last = item; else { last = ((last.next = item), item); x = more ? "a" : (item.n = x, 5); } } }
const sentinel: number = "";
export {};
// @filename: a9.ts
declare const more: boolean;
function g07() { let x: string | number = "a"; while (more) { x = more ? "a" : (x.toFixed(), 5); } }
function g07b() { let x: string | number = "a"; while (more) { x = more ? "a" : (x.toFixed(), x.toFixed(), 5); } }
function g07c() { let x: string | number = "a"; while (more) { x.toFixed(); x = more ? "a" : 5; } }
function g07d() { let x: string | number = "a"; while (more) { x = more ? "a" : (x.toFixed(), 5); x.toFixed(); } }
function g07e() { let x: string | number = "a"; while (more) { x = more ? "a" : (x.toFixed, 5); } }
function g07f() { let x: string | number = "a"; while (more) { x = more ? "a" : (x["toFixed"](), 5); } }
const sentinel: number = "";
export {};
// @filename: b0.ts
interface A { next: number } interface B { next: string }
declare const a: A, b: B, more: boolean;
export function r1() { let last: A | B = a; while (more) { last = (last.next = "s", b); } }
export function r2() { let last: A | B = a; while (more) { last = (last.next = 1, b); } }
export function r3() { let last: A | B = a; do { last = (last.next = "s", b); } while (more); }
export function r4() { let last: A | B = a; for (;;) { last = more ? (last.next = "s", b) : a; } }
const sentinel: number = "";
// @filename: b4.ts
declare const more: boolean; declare const fn: () => void; declare let numVar: number;
interface A2 { k: 1; v: 1; extra: 1 } interface B2 { k: 2; v: 2 } declare const a2: A2; declare const b2: B2;
type OnlyKV<K extends "k" | "v"> = K;
export function a1() { let f: (() => void) | undefined = fn; while (more) { f = f ? undefined : fn; } }
export function a2_() { let x: string | number = "a"; while (more) { x = more && x === 5 ? "b" : 5; } }
export function b1() { let x: string | number = "a"; while (more) { x = more ? "a" : (numVar = x, 5); } }
export function c1() { let f: string | (() => void) = "a"; while (more) { f = more ? fn : (f(), "a"); } }
export function d1() { let o: A2 | B2 = a2; while (more) { o = more ? b2 : ((null! as OnlyKV<keyof typeof o>), a2); } }
export function e1() { let x: string | number = "a"; while (more) { x = more ? "a" : (x.toFixed(), 5); } }
export function f1() { let o: A2 | B2 = a2; while (more) { o = more ? b2 : ((null! as (typeof o)["extra"]), a2); } }
const sentinel: number = "";
// @filename: b5.ts
interface Item { next: Item | null; n: number; cb(x: number): Item }
declare const more: boolean; declare const item: Item;
declare function takeNum(n: number): number;
export function r1() { let last: Item | null = null; let x: string | number = "a"; while (more) { x = last === null ? "a" : last.n; if (last === null) last = item; else last = last.cb(takeNum(x)); } }
export function r2() { let x: string | number = "a"; while (more) { x = more ? "a" : (void takeNum(x), 5); } }
export function r3() { let x: string | number = "a"; while (more) { x = more ? "a" : (takeNum(x), 5); } }
export function r4() { let last: Item | null = null; let x: string | number = "a"; while (more) { x = last === null ? "a" : last.n; if (last === null) last = item; else last = last.cb(takeNum(takeNum(x))); } }
export function r5() { let last: Item | null = null; let x: string | number = "a"; while (more) { x = last === null ? "a" : last.n; if (last === null) last = item; else last = new (last.cb as any)(takeNum(x)); } }
const sentinel: number = "";
// @filename: b6.ts
interface Result { score: number; name: string }
declare const results: Result[]; declare const byName: Record<string, Result>; declare const more: boolean;
export function u1() { let best: Result | null = null; for (const it of results) { if (best === null || it.score > best.score) best = it; } return best === null ? 0 : best.score; }
export function u2() { let best: Result | null = null; for (const it of results) { best = it; } if (best) return best.name; return ""; }
export function u3() { let best: Result | null = null; for (const key in byName) { best = byName[key]!; } return best?.score; }
export function u4() { let best: Result | null = null; for (const it of results) { if (more) continue; best = best === null ? it : best.score > it.score ? best : it; } return best !== null && best.name; }
export function u5() { let best: Result | null = null; for (const it of results) { best = it; } return best.score; }
const sentinel: number = "";
// @filename: b7.ts
declare const more: boolean; declare const fn: () => void; declare const item: { next: unknown };
export function p1() { let g: (() => void) | undefined = fn; while (more) { for (g of [g ? undefined : fn]) {} } }
export function p2() { let x: string | number = "a"; while (more) { for (x of [more && x === 5 ? "b" : 5]) {} } }
export function p3() { let g: (() => void) | string = fn; while (more) { for (g of [more ? fn : (g(), "a")]) {} } }
export function p4() { let k: string = "a"; let g: (() => void) | undefined = fn; while (more) { for (k in (g ? { a: 1 } : { b: 2 })) {} g = more ? undefined : fn; } }
export function p5() { let g: (() => void) | undefined = fn; while (more) { for ([g] of [[g ? undefined : fn]]) {} } }
export function q1(y: string) { let x: string | number = "a"; while (more) { x; x = more && y === 5 ? "b" : 5; } }
export function q2(y: string) { let x: string | number = "a"; while (more) { x = more && y === 5 ? "b" : 5; } }
export function q3(y: string) { let x: string | number = "a"; while (more) { x; x = more && (y === 5 || y === 6) ? "b" : 5; } }
export function q4(y: string) { let x: string | number = "a"; while (more) { x; x = more || y !== 5 ? "b" : 5; } }
export function q5(y: string) { let x: string | number = "a"; while (more) { if (x) {} x = y === 5 ? "b" : 5; } }
export function q6(y: string) { let x: string | number = "a"; do { x; x = more && y === 5 ? "b" : 5; } while (more); }
const sentinel: number = "";
