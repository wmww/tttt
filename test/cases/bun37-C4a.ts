// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "types": [], "lib": ["es2022"] }, "extends": "./b.json" }
// @filename: a.ts
const sentinel: number = "";
// @filename: b.json
{ "include": ["src\\**", "a.ts"] }
