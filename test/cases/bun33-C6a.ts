// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "allowJs": true, "checkJs": true } }
// @filename: a.js
const m = (require("./m"));
/** @type {m.T} */ let t;
/** @type {number} */
const sentinel = "";
// @filename: m.ts
export interface T { a: number }
export class C { x = 1 }
