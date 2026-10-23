// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "allowJs": true, "checkJs": true } }
// @filename: a.ts
import d from "./m";
export const x = d.foo;
const sentinel: number = "";
// @filename: m.js
const { x } = require("./a");
const o = { foo: x };
module.exports = o;
