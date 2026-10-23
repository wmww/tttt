// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "types": [], "lib": ["es2022"] }, "extends": ["./A.json", "./B.json"] }
// @filename: A.json
{ "compilerOptions": { "strict": false } }
// @filename: B.json
{ "extends": "./C.json" }
// @filename: C.json
{ "compilerOptions": { "strict": null } }
// @filename: a.ts
let n: number = null;
const sentinel: number = "";
