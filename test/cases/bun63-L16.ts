// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true } }
// @filename: a.ts
interface Array<T> { length: number } interface Boolean {} interface CallableFunction {} interface Function {} interface IArguments {} interface NewableFunction {} interface Number {} interface Object {} interface RegExp {} interface String {}
namespace N { export type NoInfer<T> = intrinsic; }
declare function g<T>(a: T, b: N.NoInfer<T>): { c: N.NoInfer<T[]> };
const n: 1 = g;
const sentinel: number = "";
