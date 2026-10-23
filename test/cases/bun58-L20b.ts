// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "module": "commonjs", "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["m.ts", "mid.ts"] }
// @filename: m.ts
import { p } from "./mid";
namespace obj { export interface p { t: 1 } }
const obj = { p: 1, q: p };
export = obj;
const sentinel: number = "";
// @filename: mid.ts
export { p } from "./m";
