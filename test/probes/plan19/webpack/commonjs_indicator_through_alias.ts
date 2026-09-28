// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @filename: p.js
const M = module.exports;
M.a = 1;
// @filename: q.js
const M = require("./p");
M.a;
