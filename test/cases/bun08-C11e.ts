// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import r = require("./a"); const v = { n: 1, m: r }; export = v;
const sentinel: number = "";
