// A named import of a property of an `export =` union: a member with an index signature for the name has it
// partially (tsc's createUnionOrIntersectionProperty), so `a` is 1 | 2; with no real property anywhere it is missing
// @esModuleInterop: true
// @module: commonjs
// @strict: true
// @filename: m1.ts
declare const v: { a: 1 } | { [k: string]: 2 };
export = v;
// @filename: m2.ts
declare const w: { a: 1 } | { b: 2 };
export = w;
// @filename: m3.ts
declare const u: { [k: string]: 2 };
export = u;
// @filename: a.ts
import { a as a1 } from "./m1";
const r1: boolean = a1;
import { a as a2 } from "./m2";
import { zz } from "./m3";
const r3: boolean = zz;
