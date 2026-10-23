// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const obj: { a: number }; declare let a: any;
for (([a]) in obj) {}
const sentinel: number = "";
