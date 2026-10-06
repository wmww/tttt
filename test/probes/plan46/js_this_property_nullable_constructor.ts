// JavaScript this-property types: the constructor's assignments alone only if one assigns more than null/undefined
// as assigned (not widened: without strictNullChecks that would be any), and a constructor function's
// F.prototype.x isn't the instance's this.x (webpack FileSystemInfo.js 1319 under nosnc)
// @target: es2015
// @allowJs: true
// @checkJs: true
// @noEmit: true
// @strictNullChecks: false,true
// @noImplicitAny: true
// @filename: a.js
class C {
  constructor() {
    this.x = undefined;
  }
  foo() {
    this.x = new Map();
  }
}
module.exports = C;
