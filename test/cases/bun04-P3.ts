// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "module": "commonjs" } }
// @filename: a.ts
const sentinel: number = "";
// @filename: b.js
const w = require("./m");
export class w {}
// @filename: m.ts
export const v = 1;
