// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
import * as a from "./m";
import * as b from "./m";
import * as c from "./m";
import * as d from "./m";
import * as e from "./m";
export function f() {
  const a = 1;
  const b = 1;
  const c = 1;
  const d = 1;
  const e = 1;
  /** {@link a#delete} {@link b#foo} {@link c#default} {@link d#of} {@link e.delete} */
  const q = 2;
  return q;
}
export const sentinel: number = "";
// @filename: m.ts
const x = 1;
export { x as delete, x as foo, x as default, x as of };
