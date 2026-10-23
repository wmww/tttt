// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import d from "./b";
export default d;
const n: number = d;
d.nope;
const sentinel: number = "";
// @filename: b.ts
import a = require("./a");
export = a;
// @filename: u.ts
import d from "./a";
import b from "./b";
const n: number = d;
const m: number = b;
