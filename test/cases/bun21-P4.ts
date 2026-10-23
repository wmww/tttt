// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(o: any) { with (o) { var g = () => { const n: number = ""; }; } return g(); }
const sentinel: number = "";
