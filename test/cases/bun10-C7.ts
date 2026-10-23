// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f1() { return g1(); }
const g1 = f1;
const sentinel: number = "";
