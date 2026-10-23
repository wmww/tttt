// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" }, "files": ["m.ts", "a.ts"] }
// @filename: a.ts
import { p } from "./m";
export const y = p;
const t: never = y;
const sentinel: number = "";
// @filename: m.ts
import { y } from "./a";
interface B<T> { b: T }
interface V extends B<typeof y> { p: number }
declare const v: V;
export = v;
