// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f1(a: string, b?: ReturnType<typeof g1>): string;
function f1(a: number, b?: number): number;
function f1(a: any, b?: any) { return a; }
function g1() { return f1(1, 2); }
function g2() { return f2(1, 2); }
function f2(a: string, b?: ReturnType<typeof g2>): string;
function f2(a: number, b?: number): number;
function f2(a: any, b?: any) { return a; }
declare function f3(a: string, b?: typeof v3): string;
declare function f3(a: number, b?: number): number;
const v3 = [f3(1, 2)];
class C4 {
  m(a: string, b?: ReturnType<C4["n"]>): string;
  m(a: number, b?: number): number;
  m(a: any, b?: any) { return a; }
  n() { return [this.m(1, 2)]; }
}
interface I5 { (a: string, b?: typeof v5): string; (a: number, b?: number): number }
declare const i5: I5;
const v5 = [i5(1, 2)];
declare class C6 { constructor(a: string, b?: typeof v6); constructor(a: number, b?: number); }
const v6 = [new C6(1, 2)];
declare function f7(a: string, ...b: (typeof v7)[]): string;
declare function f7(a: number, ...b: number[]): number;
const v7 = [f7(1, 2)];
declare function f8(a: string, b: typeof v8, c?: 1): string;
declare function f8(a: number, b: number, c?: 1): number;
const v8 = [f8(1, 2, 1)];
declare function f9<T>(a: string, b?: typeof v9, c?: T): string;
declare function f9(a: number, b?: number): number;
const v9 = [f9(1, 2)];
const sentinel: number = "";
