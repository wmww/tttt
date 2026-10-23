// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "node16", "allowJs": true, "checkJs": true } }
// @filename: d.js
/** @import { T } from "./esm.mjs" */
/** @type {number} */ const sentinel = "";
// @filename: esm.mts
export type T = number;
// @filename: package.json
{ "type": "commonjs" }
