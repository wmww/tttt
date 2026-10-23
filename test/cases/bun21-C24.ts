// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
k = "b"; export {}; let k = "a"; declare const rec: Record<string, string | undefined>;
if (rec[k]) rec[k].length;
const sentinel: number = "";
