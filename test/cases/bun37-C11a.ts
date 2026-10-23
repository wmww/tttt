// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "lib": ["es2022"], "types": [], "moduleDetection": "" }, "extends": "./A.json", "files": ["a.ts", "b.ts"] }
// @filename: A.json
{ "compilerOptions": { "moduleDetection": "force" } }
// @filename: a.ts
const x = 1;
const sentinel: number = "";
// @filename: b.ts
const x = 2;
