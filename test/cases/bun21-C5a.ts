// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(x: { a: number } | undefined, y: typeof x) { if ((x = y)?.a !== undefined) x.a; }
const sentinel: number = "";
