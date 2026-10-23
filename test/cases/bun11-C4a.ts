// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
async function f1(n: number) { if (n) return 1; return await await f1(n - 1); }
const sentinel: number = "";
