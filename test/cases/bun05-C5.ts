// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const arr: number[];
function ff() {}
for (ff in arr) { const x: number = arr[ff]; }
const sentinel: number = "";
