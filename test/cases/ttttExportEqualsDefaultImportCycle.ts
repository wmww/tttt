// @module: commonjs
// @strict: true
// @noEmit: true

// A default import of an `export =` module looks `default` up among the properties of what it assigns (tsc's
// resolveExportByName -> getPropertyOfType), which resolves the assigned module's export aliases: a.ts's `export default
// d` of its own import d is circular (TS2303 twice, bun case 94-R04a); without the cycle the import is the module
// @filename: a.ts
import d from "./b";
export default d;
const n: number = d;
// @filename: b.ts
import a = require("./a");
export = a;
// @filename: f.ts
const v = 42;
export default v;
// @filename: g.ts
import f = require("./f");
export = f;
// @filename: u.ts
import h from "./g";
const s: string = h;
