// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const arr: number[];
let li: string;
for ((li) in arr) { const x: number = arr[li]; }
const sentinel: number = "";
