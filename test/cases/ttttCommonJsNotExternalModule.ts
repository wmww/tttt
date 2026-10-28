// @allowJs: true
// @checkJs: true
// @noEmit: true
// @module: es2022,commonjs
// A CommonJS JavaScript file is no external module to tsc (IsExternalModule): `export as namespace` there is TS1314
// (bun 33-C14a), and a top-level `await` is TS1375 unless the emit module kind contains CommonJS
// (IsEffectiveExternalModule: bun 33-C1b)
// @filename: j.js
exports.a = 1;
export as namespace Foo;
// @filename: a.js
module.exports = 1;
await 1;
