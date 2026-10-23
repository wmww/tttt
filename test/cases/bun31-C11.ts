// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A { assertStr(x: unknown): asserts x is string {} }
function m(a: A | undefined, x: unknown) { a?.assertStr(x); }
const sentinel: number = "";
