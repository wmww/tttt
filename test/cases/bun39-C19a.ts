// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface A { a: number }
interface A$ { b: number }
class C implements A, A$ {}
const sentinel: number = "";
