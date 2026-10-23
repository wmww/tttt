// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const m: { a: { get(k: string): 1 } }, k: string;
m[`a`][k];
const sentinel: number = "";
