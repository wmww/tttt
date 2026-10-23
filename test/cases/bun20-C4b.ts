// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function e15() { let x = []; x.push(1); for (const k in x) { x.push("a"); } }
const sentinel: number = "";
