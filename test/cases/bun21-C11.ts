// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f() { const x = []; x.push(1); if (!x) x.push("a"); }
const sentinel: number = "";
