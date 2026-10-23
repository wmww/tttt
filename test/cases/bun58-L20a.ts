// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "module": "commonjs", "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["m.ts", "mid.ts", "mid2.ts", "u.ts"] }
// @filename: m.ts
namespace obj { export interface p { t: 1 } }
const obj = { p: 1 };
export = obj;
// @filename: mid.ts
export { p } from "./m";
// @filename: mid2.ts
import { p } from "./m"; export { p as p3 };
// @filename: u.ts
import { p } from "./m";
import { p as p2 } from "./mid";
import * as mid from "./mid";
import { p3 } from "./mid2";
const v: string = p; let t: p = {};
const v2: string = p2; let t2: p2 = {};
const v3: string = mid.p; let t3: mid.p = {};
const v4: string = p3; let t4: p3 = {};
const sentinel: number = "";
