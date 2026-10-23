// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace N { export function f<T>(x: T) { class C { a!: T } return new C(); } }
const o: 1 = { p: N.f(1) };
const sentinel: number = "";
