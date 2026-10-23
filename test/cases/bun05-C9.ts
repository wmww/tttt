// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const arr: number[];
for (gi in arr) { const x: number = arr[gi]; }
const sentinel: number = "";
// @filename: b.ts
var gi: string;
