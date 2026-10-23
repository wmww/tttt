// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function g<K extends "a">(x: { [P in keyof typeof obj]: number }[K]): number;
const obj = { a: g(1) };
const sentinel: number = "";
