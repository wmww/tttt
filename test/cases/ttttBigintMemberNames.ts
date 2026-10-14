// Bigint member names declare no member (tsc: __missing outside the table): TS1539, no duplicates, symbols of their own
// @target: esnext
const a = { 1n: 1, 2n: 2, 1n: 3 };
class C { 1n = 0; 2n() {} get 3n() { return 1; } }
interface I { 1n: string; 2n(): void; }
const i: I = { 1n: "a" };
const c = new C();
declare const o: { a: number };
const { 1n: x, ...rest } = o;
let y: { 5n?: number } = {};
class D { 1n = 0; 1n = 1; }
interface J { 2n: string; 2n: number; }
