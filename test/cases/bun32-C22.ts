// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(readonly h = 1) { const t: 1 = h; }
const sentinel: number = "";
