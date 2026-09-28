// @strict: true
// @noEmit: true
// @allowJs: true
// @checkJs: true
// @module: commonjs
// tsc: clean. `const X = require("m").X` is an alias of a property of the module's type (getCommonJSPropertyAccess):
// a type-only export (acorn's interface Position) or a missing one makes it unknown, silently, and a typedef of the
// same name doesn't conflict (TS2440) (webpack's registerExternalSerializer.js)
// @filename: m.d.ts
export interface Position { line: number }
export class Loc { constructor(p: Position); }
// @filename: a.js
const Position = require("./m").Position;
const Loc = require("./m").Loc;
const Nope = require("./m").Nope;
/** @typedef {import("./m").Position} Position */
/** @param {Position} p */
function f(p) { return new Loc(p); }
new Position();
new Nope();
module.exports = f;
