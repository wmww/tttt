// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import * as u from "u";
const sentinel: number = "";
// @filename: node_modules/u/index.js
module.exports = {};
// @filename: node_modules/u/package.json
{ "name": "", "version": "1.0.0" }
