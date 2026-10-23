// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(o?: { a?: 1 }) { if (o?.a! !== undefined) o.a; }
const sentinel: number = "";
