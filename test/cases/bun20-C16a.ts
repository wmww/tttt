// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A { a = 1 }
function z1<TA extends A>(x: Partial<TA>) { switch (x) { case "a": break; } }
const sentinel: number = "";
