// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { m(): { [K in keyof this]: 1 } }
interface A extends I { a: 1 }
interface B extends I { b: 1 }
function f<T extends I>(x: T) { return x.m(); }
const r = f({} as A | B);
const r1: never = r;
const sentinel: number = "";
