// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @filename: w.js
/**
 * @typedef {object} Cbs
 * @property {((input: string) => number)=} identifier
 */
/** @param {number=} pos */
module.exports = (pos = 0) => pos;
/** @param {Cbs} callbacks */
const consumeUntil = (callbacks) => 0;
module.exports.consumeUntil = consumeUntil;
// @filename: c.js
class C { constructor() { this.q = 1; } }
module.exports = C;
module.exports.x = 1;
module.exports.D = class D {};
// @filename: m.js
const w = require("./w");
w.consumeUntil({ identifier(input) { return input.length; } });
/** @type {string} */ const y = w;
const c = require("./c");
/** @type {string} */ const z = c.x;
/** @type {c.D} */ const d = 1;
/** @type {import("./c").D} */ const d2 = 1;
/** @type {c.x} */ const d3 = "";
