// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "module": "commonjs", "types": [], "lib": ["es2022"] } }
// @filename: a1.js
class C { p = [this.p, module]; }
module.exports = C;
// @filename: a2.js
class C { static p = [C.p, exports]; }
exports.C = C;
// @filename: a3.js
function f() {}
f.p = [f.p, module];
module.exports = f;
// @filename: a4.js
exports.a = [exports.a, module];
// @filename: zz.ts
export {};
const sentinel: number = "";
