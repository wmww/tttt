// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function m() { let a: number; ([a]) = [1]; a; }
const sentinel: number = "";
