// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(x: number | undefined) { x! = 1; x.toFixed(); }
const sentinel: number = "";
