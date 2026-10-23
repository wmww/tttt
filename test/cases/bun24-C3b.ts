// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace N { export function f<T>(x: T) { class L { v!: T } return new L(); } }
const e: 1 = { c: N.f(1) };
const sentinel: number = "";
