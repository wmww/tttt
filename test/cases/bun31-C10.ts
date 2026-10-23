// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function m() { let a: string | number; a! = 1; const n: number = a; }
const sentinel: number = "";
