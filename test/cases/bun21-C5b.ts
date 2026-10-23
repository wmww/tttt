// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type O = { a: number } | undefined;
function f(x: O) { if ((x satisfies O)?.a !== undefined) { x.a; } }
const sentinel: number = "";
