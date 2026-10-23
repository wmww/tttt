// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C3 { m(n: number) { const m = this.m; if (n) return 1; return m(n - 1); } }
const sentinel: number = "";
