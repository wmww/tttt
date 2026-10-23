// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type U = { k: "a"; v: string } | { k: "b"; v: number };
function f(u: U | undefined) { if ((u!).k === "a") u!.v.length; }
const sentinel: number = "";
