// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import * as ns from "./b";
export function fa() { return ns.foo; }
ns.nope;
const sentinel: number = "";
// @filename: b.ts
import A = require("./a");
const o = { foo: 1, get g() { return A.fa(); }, default: 1 };
export = o;
