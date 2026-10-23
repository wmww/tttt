// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function g(x: typeof obj) { return x; }
const obj = { a: g(1 as any) };
const sentinel: number = "";
