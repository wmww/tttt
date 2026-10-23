// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function need(n: number): number;
const a1: number = Symbol.iterator;
var d1 = { x: need(Symbol.iterator), y: d1 };
const sentinel: number = "";
