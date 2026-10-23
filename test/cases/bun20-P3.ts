// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function b(x?: { a: number }) { if (x?.a! === 1) { x.a; } }
const sentinel: number = "";
