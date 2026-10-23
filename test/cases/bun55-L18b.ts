// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function one<T>(v: T): { a: T; b: T };
const { a: [n, s], b = JSON.parse("[]") } = one([1, "hi"]);
s.length;
// without the default of type `any` the pattern makes the argument a tuple in both
const { a: [n2, s2], b: b2 } = one([1, "hi"]);
s2.length;
const sentinel: number = "";
