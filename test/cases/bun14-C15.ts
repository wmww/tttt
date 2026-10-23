// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "exactOptionalPropertyTypes": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type X = { p?: string; q?: number };
type A = X["p" | "q"];
declare let a: A;
const n1: null = a;
const sentinel: number = "";
