// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts"] }
// @filename: a.ts
/// <reference path="./upper.TS" />
const a: 1 = upperGlobal;
const sentinel: number = "";
// @filename: upper.TS
declare const upperGlobal: 1;
