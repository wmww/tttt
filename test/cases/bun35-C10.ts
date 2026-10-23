// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext", "customConditions": ["import"] } }
// @filename: b.cts
import u1 = require("untyped");
const sentinel: number = "";
// @filename: node_modules/untyped/js/index.js
module.exports = {};
// @filename: node_modules/untyped/package.json
{ "name": "untyped", "version": "1.0.0", "types": "./types/index.d.ts", "exports": { ".": "./js/index.js" } }
// @filename: node_modules/untyped/types/index.d.ts
export const v: 1;
