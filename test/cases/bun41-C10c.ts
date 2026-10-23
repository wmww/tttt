// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f() { interface A { a: number } return null! as A; }
function g() { interface A { x: { y: number } } return null! as A; }
type A1 = ReturnType<typeof f>;
type A2 = ReturnType<typeof g>;
interface I extends A1, A2 { a: string; x: { y: string } }
const sentinel: number = "";
