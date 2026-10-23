// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "allowJs": true, "checkJs": true } }
// @filename: a.js
const { ["a"]: c1 } = require("./esm2"); c1.nope;
const { 0: n1 } = require("./esm2");
/** @type {number} */ const sentinel = "";
// @filename: esm2.ts
export const a = 1;
