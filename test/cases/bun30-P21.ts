// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.ts
const sentinel: number = "";
// @filename: f.js
exports.a = function () {};
a.b = 1;
exports.a.b;
