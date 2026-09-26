// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @filename: a.js
const Strings = { a: "A" };
class Foo {}
exports.x = 1;
exports.y = Strings;
module.exports.z = "z";
function f() { return exports.x; }
// @filename: b.js
const a = require("./a");
const { x } = require("./a");
a.x;
a.y.a;
x;
// @filename: c.ts
import c = require("./a");
c.z;
