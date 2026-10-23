// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface C { kind: string; m(): this extends { kind: "a" } ? 1 : 2 }
interface A extends C { kind: "a" }
interface B extends C { kind: "b" }
function f<T extends C>(t: T) { return t.m(); }
const r: 1 | 2 = f(null! as A | B);
const sentinel: number = "";
