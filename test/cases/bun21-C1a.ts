// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function helper(p: number) { return compute(p); }
function compute(v: number) { helper(v); return v; }
const sentinel: number = "";
