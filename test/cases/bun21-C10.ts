// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const rec: Record<string, string | undefined>;
function outer() { let n: number; function inc(k: string) { if (rec[k]) rec[k]; n++; } n = 0; }
const sentinel: number = "";
