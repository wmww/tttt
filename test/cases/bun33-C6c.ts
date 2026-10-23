// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "allowJs": true, "checkJs": true } }
// @filename: a.js
const [a] = require("./m");
/** @type {number} */
const sentinel = "";
// @filename: m.ts
export interface T { a: number }
export class C { x = 1 }
