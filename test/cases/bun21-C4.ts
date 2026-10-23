// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const n: number;
const k = compute(1);
function compute(e: 1 | 2) { switch (e) { case n: return 1; case k: return 2; } }
const sentinel: number = "";
