// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function g(o?: { a?: { b: 1 } }) { if ((o?.a)?.b === 1) o.a; }
const sentinel: number = "";
