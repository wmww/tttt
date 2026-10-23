// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "allowJs": true, "checkJs": true } }
// @filename: a.js
const k = "C";
const { [k]: D } = require("./m");
/** @type {D} */ let t;
/** @type {number} */
const sentinel = "";
// @filename: m.ts
export interface T { a: number }
export class C { x = 1 }
