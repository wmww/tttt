// A CommonJS `module.exports.T = T` merged with `@typedef T` and no other T: the alias finds itself by its type
// meaning and is its own target, with no value meaning (tsgo's getSymbolFlags stops at `target === symbol`): TS2693,
// and no TS2309 for the `module.exports =` (seed 20261111, jsTypedefMergedWithModuleExportProperty delete-stmt)
// @allowJs: true
// @checkJs: true
// @target: es2015
// @outDir: ./out
// @declaration: true

// @filename: local-lib/ModuleGraphConnection.js
/** @typedef {typeof T} T */
module.exports = class ModuleGraphConnection {};
module.exports.T = T;

// @filename: repro.js
'use strict';
/** @typedef {import('./local-lib/ModuleGraphConnection')} ImportedType */
/** @type {ImportedType} */
module.exports = class Repro {};
