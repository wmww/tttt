// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "types": [], "lib": ["es2022"] }, "extends": "./sub/b.json" }
// @filename: a.ts
const sentinel: number = "";
// @filename: sub/b.json
{ "include": ["**\\..\\x", "../a.ts"] }
// @filename: sub/x/x.ts
const x: number = "";
