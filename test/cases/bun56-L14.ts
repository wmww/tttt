// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let a = 1 as const<>;
let a2 = <const<>>1;
let a3 = [1, "x"] as const<>;
const n: 2 = a;
const sentinel: number = "";
