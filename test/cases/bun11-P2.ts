// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function g(x: keyof typeof obj): number;
const obj = { a: 1, b: g("a") };
const sentinel: number = "";
