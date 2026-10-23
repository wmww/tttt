// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type M = { [K in keyof N as A["a"] & string]: M["a"] };
type A = { a: N["a"]; b: 1 };
type N = { [P in "a" | "b"]: M["b"] };
let m: M = 1;
const sentinel: number = "";
