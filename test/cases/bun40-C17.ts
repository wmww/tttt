// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts"] }
// @filename: a.ts
/// <reference types="bar" />
/// <reference types="Bar" />
const sentinel: number = "";
// @filename: node_modules/bar/index.d.ts
declare const bar: 1;
// @filename: node_modules/bar/package.json
{"name":"bar","version":"4.5.6","types":"index.d.ts"}
