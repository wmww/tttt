// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// tsc: TS2580 on `module` (its binder declares the CommonJS `module` at a `module.exports` only once the module
// indicator is set, which the later `M.a = 1` does); tttt declares it for the whole file. q.js is fine in both.
// @filename: p.js
const M = module.exports;
M.a = 1;
// @filename: q.js
const M = require("./p");
M.a;
