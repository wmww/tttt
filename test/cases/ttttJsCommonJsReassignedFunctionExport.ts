// @allowJs: true
// @checkJs: true
// @declaration: true
// @emitDeclarationOnly: true
// @target: es2022
// @module: commonjs
// A CommonJS export assigned several function expressions has the union of their types, the first one isn't the
// type (tsc's getWidenedTypeForAssignmentDeclaration unions every assignment's; mobx's __tests__/perf/index.js), nor
// is an expando's

// @filename: a.js
if (Math.random()) {
    exports.f = function (msg) { console.log(msg) }
} else {
    exports.f = function (msg) { }
}
exports.h = function () { };
exports.h.x = 1;
exports.h = function (x) { };
class C {}
C.k = function () {};
C.k = function (y) {};
module.exports.C = C;
