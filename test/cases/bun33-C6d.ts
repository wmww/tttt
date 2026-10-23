// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "allowJs": true, "checkJs": true } }
// @filename: a.js
const { ["C"]: D } = require("./m");
/** @type {D} */ let t = 1;
/** @type {number} */
const sentinel = "";
// @filename: m.ts
export interface T { a: number }
export class C { x = 1 }
