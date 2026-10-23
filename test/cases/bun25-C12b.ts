// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let w: number; const o = { w = 1 }; w.toFixed();
const sentinel: number = "";
