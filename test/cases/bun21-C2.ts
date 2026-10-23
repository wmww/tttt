// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const key = compute({ a: 1 }, {});
function compute(o: { a: number }, p: any) { p[key] = 1; return o.a; }
const sentinel: number = "";
