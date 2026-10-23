// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
(function f(this: any, a) { const n: never = f; })(1);
const sentinel: number = "";
