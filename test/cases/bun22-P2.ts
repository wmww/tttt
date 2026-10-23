// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T extends { b: number }>(x: T): T;
declare const v: any;
namespace N { export const b = (f(v) satisfies typeof N, 1); }
const sentinel: number = "";
