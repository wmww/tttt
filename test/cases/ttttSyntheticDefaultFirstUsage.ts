// @strict: true
// @module: nodenext
// @noTypesAndSymbols: true
// @noEmit: true
// A module type's synthetic default is decided by the first import that asks (tsc's getTypeWithSyntheticDefaultImportType
// caches it on the type): a CommonJS file's static `import * as` of a CommonJS module says no, and a later
// `await import()` of it, which says yes on its own, gets the module without one; an `export =` value's `__esModule`
// property counts as the module's marker (resolveExportByName) (bun's 08-C7a, 08-C7b)
// @filename: cjs.cts
export const a = 1;
export default 5;
// @filename: fn.d.cts
declare function f(): void;
declare namespace f { const __esModule: true; const x: number }
export = f;
// @filename: a.cts
import * as cns from "./cjs.cjs";
import * as fns from "./fn.cjs";
async function dyn() {
  const c = await import("./cjs.cjs");
  c.default.a;
  const d = await import("./fn.cjs");
  d.default();
}
