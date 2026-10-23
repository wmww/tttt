// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(x?: { a: object }) { for (const k in (x?.a)) { x.a; } }
const sentinel: number = "";
