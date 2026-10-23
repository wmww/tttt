// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f11(n: number) { if (n) return 1; return (f11)(n - 1); }
const sentinel: number = "";
