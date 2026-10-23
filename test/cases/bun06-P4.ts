// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "module": "commonjs" } }
// @filename: c.js
let x;
function f() { return x; }
module.exports = { f };
// @filename: z.ts
const sentinel: number = "";
