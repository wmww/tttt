// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A { foo() {} }
class B extends A { static constructor() { super.foo; } }
const sentinel: number = "";
