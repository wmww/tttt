// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts"] }
// @filename: a.ts
/// <reference path="./b.TS" />
b;
const sentinel: number = "";
// @filename: b.ts
declare const b: 1;
