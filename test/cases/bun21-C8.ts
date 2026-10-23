// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { a?: string };
if (o[E.A]) o[E.A].length;
const enum E { A = "a" }
const sentinel: number = "";
