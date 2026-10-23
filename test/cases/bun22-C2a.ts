// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T>(x: { [k: string]: any; a: T }): T;
namespace N { export const a = 1; export const b = f(N); }
const sentinel: number = "";
