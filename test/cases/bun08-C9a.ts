// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "allowJs": true, "checkJs": true } }
// @filename: a.js
const { ...rest2 } = require("./esm2");
const [arr2] = require("./esm2");
/** @type {number} */ const sentinel = "";
// @filename: esm2.ts
export const a = 1;
