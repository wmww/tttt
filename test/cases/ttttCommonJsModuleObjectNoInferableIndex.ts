// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @module: commonjs
// A CommonJS file's `module` is `{ exports: ... }` with `module`'s symbol (SymbolFlags.ModuleExports): no inferable
// index (tsgo's isObjectTypeWithInferableIndex), so it isn't assignable to a Record
// @filename: j.js
/** @type {Record<string, unknown>} */
const r = module;
module.exports.x = 1;
