// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface A { constructor: number }
interface B { constructor: string }
class X { constructor() {} }
interface X extends A, B {}
const sentinel: number = "";
