// origin: npm tybrs-lighthouse@1.0.0 https://github.com/googlechrome/lighthouse tsconfig.bench.json seed 20261217, core/gather/driver/network-monitor.js:60
// flip: no
// A JavaScript `this.x = []` of a class whose body declares x (a field with a JSDoc type, or not) is typed by that
// declaration: tsc never widens the assignment (getWidenedTypeForAssignmentDeclaration), no TS7008 there
// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @filename: a.js
class M {
  /** @type {Array<string>} */
  _nav = [];
  /** @type {number[]} */
  _plain = [];
  _untyped = [];
  constructor() {
    this._on = () => this._nav.push("x");
  }
  enable() {
    this._nav = [];
    this._plain = [];
    this._untyped = [];
  }
}
