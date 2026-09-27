// @noEmit: true
// @allowJs: true
// @checkJs: true
// @filename: X.js
class X { constructor(a) { this.a = a; } }
module.exports = X;
// @filename: m.js
function f(o) {
  if (o) {
    const X = require("./X");
    new X(1);
  } else {
    const X = require("./X");
    new X(1);
  }
  { const Y = require("./X"); }
  Y;
}
module.exports = f;
