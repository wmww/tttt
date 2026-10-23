// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f2(n: number) { const g = f2; if (n) return 1; return g(n - 1); }
const sentinel: number = "";
