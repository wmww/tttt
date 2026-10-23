// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T>(x: { [k: string]: any }, y: T): T;
namespace N { export const b = f(N, 1); }
const sentinel: number = "";
