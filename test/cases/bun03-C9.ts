// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface A { a: number }
interface B { a: string }
class X { constructor(public a: any) {} }
interface X extends A, B {}
const sentinel: number = "";
