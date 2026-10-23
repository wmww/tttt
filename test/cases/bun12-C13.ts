// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { self(): this; p: { a: 1 } }
interface I { q: 1 }
interface J extends I { j: 1 }
type U1 = I["p"] | J["p"];
const n: never = null! as U1;
const sentinel: number = "";
