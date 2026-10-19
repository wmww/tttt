// @allowJs: true
// @checkJs: true
// @declaration: true
// @emitDeclarationOnly: true
// @module: commonjs
// `module.exports = { f }` whose type names a JSDoc typedef of a linked package's file its exports don't expose: TS2883
// at the assignment, named '(Missing)' (tsgo's transformExportAssignment through visitCJSExportAssignments;
// markdown-magic's argparse), and no declaration file

// @filename: /packages/args/package.json
{ "name": "@x/args", "types": "types/index.d.ts", "exports": { ".": { "types": "./types/index.d.ts", "require": "./src/index.js", "default": "./src/index.js" } } }

// @filename: /packages/args/src/index.js
const { dxParse } = require('./p')
module.exports = { dxParse }

// @filename: /packages/args/src/p.js
/**
 * @typedef {Object} ParseOptions
 * @property {boolean} [x]
 */
/**
 * @param {string[]} a
 * @param {ParseOptions} [o]
 */
function dxParse(a, o) { return 1 }
module.exports = { dxParse }

// @filename: /packages/core/src/a.js
const { dxParse } = require('@x/args')
module.exports = {
  dxParse,
}
// @link: /packages/args -> /packages/core/node_modules/@x/args
