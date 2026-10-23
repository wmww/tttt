// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const helper: (n: number) => typeof k;
const k = compute(1);
function compute(v: number) { helper(1); return v; }
const sentinel: number = "";
