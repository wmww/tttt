// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const b1: { [Symbol.iterator](): { next(): IteratorResult<number>; return: number } };
const r1 = f1();
function f1() { let a: number | string; [a] = b1; return a; }
declare const b2: { [Symbol.iterator](): { next(): IteratorResult<number>; return: string } };
const r2 = f2();
function f2() { let a: number | string, b: number[] | string; [a, ...b] = b2; return b; }
declare const b3: { [Symbol.iterator](): { next(): IteratorResult<number>; return: boolean } };
const r3 = f3();
function f3() { let a: number | string; ({ p: [a] } = { p: b3 }); return a; }
// the function first, the call after: both tools report
declare const b4: { [Symbol.iterator](): { next(): IteratorResult<number>; throw: number } };
function f4() { let a: number | string; [a] = b4; return a; }
const r4 = f4();
const sentinel: number = "";
