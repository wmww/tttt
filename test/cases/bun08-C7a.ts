// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext" } }
// @filename: a.cts
import * as cns from "./cjs.cjs";
async function dyn() {
  const c = await import("./cjs.cjs");
  c.default.a;
}
const sentinel: number = "";
// @filename: cjs.cts
export const a = 1;
export default 5;
