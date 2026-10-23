// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "node16" }, "files": ["a.mts"] }
// @filename: a.mts
import { kind as k1 } from "dual";
import { b } from "./b.mjs";
import k2 = require("dual");
declare const o: Order; o.f(0);
const sentinel: number = "";
// @filename: b.mts
export const b = 1;
declare global { interface Order { f(x: "b"): 3 } }
// @filename: node_modules/dual/cjs.d.cts
export declare const kind: "cjs";
declare global { interface Order { f(x: "cjs"): 2 } }
// @filename: node_modules/dual/esm.d.mts
export declare const kind: "esm";
declare global { interface Order { f(x: "esm"): 1 } }
// @filename: node_modules/dual/package.json
{"name":"dual","version":"1.0.0","exports":{".":{"import":{"types":"./esm.d.mts"},"require":{"types":"./cjs.d.cts"}}}}
