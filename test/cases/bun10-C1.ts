// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function g4() { return f4(1, 2); }
function f4(a: number, b = g4()) { return a; }
const sentinel: number = "";
