// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext" } }
// @filename: a.cts
import * as fns from "./fn.cjs";
async function dyn() {
  const c = await import("./fn.cjs");
  c.default();
}
const sentinel: number = "";
// @filename: fn.d.cts
declare function f(): void;
declare namespace f { const __esModule: true; const x: number }
export = f;
