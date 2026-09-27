// @checkJs: true
// @allowJs: true
// @noEmit: true
// @module: commonjs
// @esModuleInterop: true
// @strict: true
// @filename: a.js
class A { constructor() { this.x = 1; } }
A.S = 10;
module.exports = A;
// @filename: b.js
const A = require("./a");
const a = new A();
/** @param {number} n */
function f(n) { return n; }
module.exports = f;
// @filename: c.js
const f = require("./b");
f(1);
