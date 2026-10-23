// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A { isB(): this is B { return false; } }
class B extends A { b = 1; }
function f(x: A) { if ((x.isB)()) x.b; }
const sentinel: number = "";
