// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let n: number = (x: { 1n: 6 }) => 1;
const sentinel: number = "";
