// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts"] }
// @filename: a.ts
/// <reference path="./j.JS" />
/// <reference path="./nope.TS" />
const sentinel: number = "";
// @filename: j.JS
var j = 1;
