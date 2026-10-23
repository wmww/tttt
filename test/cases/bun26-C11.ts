// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type H<X, T> = { p: T extends string ? X : 2 };
function f<A, B>(x: H<B, A>["p"] | H<A, A | B>["p"]) { const n: never = x; }
const sentinel: number = "";
