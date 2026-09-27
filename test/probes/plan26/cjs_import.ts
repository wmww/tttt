// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @esModuleInterop: true
// @module: commonjs
// @filename: a.js
class Foo { constructor() { this.a = 1; } }
module.exports = Foo;
// @filename: b.js
exports.x = 1;
exports.f = function (n) { return n; };
// @filename: c.ts
import Foo = require("./a");
import * as B from "./b";
import D from "./a";
import { x } from "./b";
import E from "./b";
new Foo();
new D();
B.x;
B.f(1);
x;
E.x;
