// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true } }
// @filename: a.ts
interface Boolean {} interface CallableFunction {} interface Function {} interface IArguments {} interface NewableFunction {} interface Number {} interface Object {} interface RegExp {} interface String {}
declare class Array<T> { filter<S extends T>(f: (x: T) => x is S): S[]; filter(f: (x: T) => unknown): T[] }
declare const u: string[] | number[];
const r1 = u.filter(x => true); const t1: 1 = r1;
declare const v: readonly string[] | number[];
const r2 = v.filter(x => true); const t2: 1 = r2;
const sentinel: number = "";
