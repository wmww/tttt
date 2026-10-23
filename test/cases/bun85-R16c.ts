// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" }, "files": ["a.ts", "m.ts"] }
// @filename: a.ts
import { p } from "./m";
export function g() { return [p]; }
const t: never = g;
const sentinel: number = "";
// @filename: m.ts
import { g } from "./a";
const v = { get p() { return g(); } };
export = v;
