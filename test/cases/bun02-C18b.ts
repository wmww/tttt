// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "module": "commonjs" } }
// @filename: a.js
module.exports = function () {};
if (typeof exports === "object" && module.exports) {}
// @filename: z.ts
const sentinel: number = "";
