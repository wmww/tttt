// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type M = { [K in "a" | "b" as A]: N["a"] };
type A = M["a"] & string;
type N = { [P in "a" | "b"]: M["b"] };
const sentinel: number = "";
