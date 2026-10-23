// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const n: { b: { get(k: string): number } }, k: string;
n[`b`][k];
const sentinel: number = "";
