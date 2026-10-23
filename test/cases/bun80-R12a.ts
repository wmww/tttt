// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "allowJs": true, "checkJs": true, "module": "commonjs", "moduleDetection": "force" } }
// @filename: a.js
exports.a = 1;
{ export { a as a2 } from "./missing"; }
/** @type {number} */ const sentinel = "";
// @filename: b.js
exports.a = 1;
{ export * as a6 from "./missing"; }
// @filename: c.js
exports.a = 1;
{ export { zzz as a2 }; }
