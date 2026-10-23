// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "module": "commonjs", "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: m4.ts
import m4 = require("./m4");
const obj = { p: 1, q: m4.p };
export = obj;
const sentinel: number = "";
