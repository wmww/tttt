// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "module": "commonjs", "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: m.ts
import { p } from "./m";
const obj = { p: 1, q: p };
export = obj;
const sentinel: number = "";
