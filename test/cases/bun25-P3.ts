// @filename: tsconfig.json
{ "compilerOptions": { "strict": false, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true }, "files": ["a1.js", "a2.js", "a3.ts"] }
// @filename: a1.js
var obj = {};
// @filename: a2.js
var v = obj.missing;
// @filename: a3.ts
var v: string;
const sentinel: number = "";
