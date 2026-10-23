// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "module": "commonjs", "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["m.ts"] }
// @filename: m.ts
import { p } from "./m";
namespace obj { export interface p { t: 1 } }
const obj = { p: 1, q: p };
export = obj;
const sentinel: number = "";
