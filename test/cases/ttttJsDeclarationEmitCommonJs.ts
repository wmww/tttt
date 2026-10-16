// @declaration: true
// @allowJs: true
// @checkJs: true
// @target: es2022
// @module: commonjs
// @outDir: ./out
// JavaScript declaration emit (plan 64 stage 6): a CommonJS module's `module.exports =` first, then each `exports.x =`,
// then the statements, with what they name painted in place; `require`s become imports, typedefs type aliases before
// their statement, `this.x` members properties, `@overload`s the declarations, a class's expandos a namespace
// @filename: lib.js
/** @typedef {{ x: number }} Coord */
class Point {
    constructor() {
        /** The x coordinate */
        this.x = 1;
        this["y-z"] = "a";
    }
}
module.exports = { Point };
module.exports.Point = Point;
// @filename: index.js
const { Point } = require("./lib");
/** @typedef {{ a: number }} Options */
/**
 * @param {Options} o
 */
function make(o) { return o.a; }
exports.version = "1.0";
exports.make = make;
exports.Sub = class Sub { self = new Sub(); };
exports.Maker = class Maker {
    constructor() {
        this.made = make({ a: 1 });
        this.version = exports.version;
    }
};
// @filename: esm.js
/** @import { Coord } from "./lib" */
/**
 * @overload
 * @param {number} x
 * @returns {number}
 */
/**
 * @overload
 * @param {string} x
 * @returns {string}
 */
/**
 * @param {unknown} x
 */
export function id(x) { return x; }
export class Holder {
    constructor() {
        /** @type {Coord | undefined} */
        this.point = undefined;
    }
}
Holder.count = 0;
