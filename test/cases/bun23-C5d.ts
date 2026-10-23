// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace N1 { export function f<T>(x: T) { class C { a!: T } return new C(); } }
namespace N2 { export function f<T>(x: T) { class C { b!: T } return new C(); } }
let a1 = N1.f(1); a1 = N2.f(1);
const sentinel: number = "";
