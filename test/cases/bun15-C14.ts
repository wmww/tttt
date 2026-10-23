// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const kn = 1;
const o = { a: 1, 1: 2, s: 3, [kn]: 4 };
const n: never = o;
const sentinel: number = "";
