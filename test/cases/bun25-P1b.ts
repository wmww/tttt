// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface J<T> { m: T extends string ? 1 : 2 }
function f<A, B, Z>(a: [J<A | B>["m"] | J<Z>["m"]]) { const n: never = a; }
const sentinel: number = "";
