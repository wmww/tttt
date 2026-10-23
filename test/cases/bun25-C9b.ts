// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(x: 0 | 1) { x! = x + 1; }
const sentinel: number = "";
