// @allowJs: true
// @checkJs: true
// @declaration: true
// @declarationMap: true
// @emitDeclarationOnly: true
// @module: commonjs
// CommonJS declarations' names are mapped (markdown-magic): `exports.x = a`'s `export { a as x }` maps `a` to the value
// and `x` to the left's property name; `const x = require("m")`'s `import x = require("m")` its name and specifier;
// `exports.x = e`'s variable its name, and no export's type (`module.exports = { v: 1 }`'s `v` neither: the node builder's)

// @filename: dep.js
module.exports = { v: 1 };

// @filename: a.js
const dep = require('./dep')
function loadConfig() { return dep.v }
function toposort() { return 1 }
module.exports.loadConfig = loadConfig
module.exports.array = toposort
module.exports.dep = dep

// @filename: c.js
const q = 1
exports.o = { v: 1 }
module.exports.p = { w: q }
