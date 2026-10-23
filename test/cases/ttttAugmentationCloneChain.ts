// @strict: true
// @module: esnext
// @moduleResolution: bundler
// @noEmit: true
// Augmentations merge in the program's order, each into a clone (tsc's mergeSymbol), and what a name means is the end
// of its chain of merged symbols: m/node's own augmentation clones R (d), m's through `export { R }` clones that clone
// (c), and m/b's through `export *` goes into the first clone in place, after it was cloned: b is lost to everyone
// @filename: node_modules/m/package.json
{ "name": "m", "exports": { ".": "./index.js", "./b": "./b.js", "./node": "./node.js" } }
// @filename: node_modules/m/node.d.ts
export interface R { base: 1 }
export declare const R: R;
// @filename: node_modules/m/b.d.ts
export * from "./node.js";
// @filename: node_modules/m/index.d.ts
export { R } from "./node.js";
// @filename: aug0.d.ts
declare module "m/node" { export interface R { d: 1 } }
export {}
// @filename: aug3.d.ts
import "m";
declare module "m" { export interface R { c: 1 } }
// @filename: aug2.d.ts
import "m/b";
declare module "m/b" { export interface R { b: 1 } }
// @filename: use.ts
import { R } from "m";
import { R as RB } from "m/b";
import { R as RN } from "m/node";
declare const t: R; t.b; t.c; t.d;
declare const tb: RB; tb.b; tb.c; tb.d;
declare const tn: RN; tn.b; tn.c; tn.d;
RN.b; RN.c; RN.d;
