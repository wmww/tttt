// @strict: true
// @allowJs: true
// @checkJs: true
// @noEmit: true
// `await` as a name at the top level is TS1262 in an ES module only: tsc's binder asks IsExternalModule, which a
// CommonJS file (`exports.x =`) isn't (bun 30-C13)
// @filename: f.js
const await = 1;
exports.x = 1;
// @filename: g.js
const await = 1;
export const y = 1;
