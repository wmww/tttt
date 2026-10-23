// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.js
function f() {}
f.a = (f.a, exports);
exports.b = 1;
/** @type {number} */
const sentinel = "";
// @filename: b.js
class C {
  x = (new C().x, module);
}
exports.a = 1;
// @filename: c.js
class C {
  static x = (C.x, exports);
}
exports.a = 1;
