// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o1 = { get d(): number { return 1 }, set d(v: string | number) {} };
const sp = { ...o1 }; sp.d = "x";
const s1: string = sp;
interface I { get d(): number; set d(v: string | number); r: 1 }
declare const i: I;
const { r, ...rest } = i; rest.d = "x";
// C8 of the report: the symbol that is handed on also keeps CheckFlagsReverseMapped.
interface IB { a: { v: number }; b: { v: string } } declare const ib: IB;
declare function un7<T>(b: { [K in keyof T]: { [L in keyof T[K]]: { v: T[K][L] } } }): T;
const e = un7({ p: ib, q: { r: { v: 1 } } });
const r8: string = { ...e };
const sentinel: number = "";
