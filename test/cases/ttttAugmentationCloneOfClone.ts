// @strict: true
// @module: esnext
// @moduleResolution: bundler
// @noEmit: true
// tsc's mergeModuleAugmentation never changes a symbol it bound: each merge goes into a clone of what the name resolves
// to, which getMergedSymbol answers for the original (one level). Augmenting one interface through several paths
// keeps them apart: m's `export { R }` gets a clone with c, m/a's and m/b's (`export *`) clones with a and b, and
// node.d.ts's own `R` (the type of the value) means the last clone made of it. And `sst` augmented through an
// `export { Resource } from` of an `export *` module that was augmented first clones that clone: opencode's
// function/api.ts, where `Resource.GITHUB_APP_ID` is TS2339
// @filename: node_modules/m/package.json
{ "name": "m", "exports": { ".": "./index.js", "./a": "./a.js", "./b": "./b.js" } }
// @filename: node_modules/m/node.d.ts
export interface R { base: 1 }
export declare const R: R;
// @filename: node_modules/m/a.d.ts
export * from "./node.js";
// @filename: node_modules/m/b.d.ts
export * from "./node.js";
// @filename: node_modules/m/index.d.ts
export { R } from "./node.js";
// @filename: node_modules/sst/package.json
{ "name": "sst", "exports": { ".": "./dist/index.js", "./resource": "./dist/resource/index.js" } }
// @filename: node_modules/sst/dist/index.d.ts
export { Resource } from "./resource/index.js";
// @filename: node_modules/sst/dist/resource/index.d.ts
export * from "./node.js";
// @filename: node_modules/sst/dist/resource/node.d.ts
import type { Resource as BaseResource } from "./shared.js";
export interface Resource extends BaseResource {}
export declare const Resource: Resource;
// @filename: node_modules/sst/dist/resource/shared.d.ts
export interface Resource { App: { name: string } }
// @filename: aug1.d.ts
import "m/a";
declare module "m/a" { export interface R { a: 1 } }
// @filename: aug2.d.ts
import "m/b";
declare module "m/b" { export interface R { b: 1 } }
// @filename: aug3.d.ts
import "m";
declare module "m" { export interface R { c: 1 } }
// @filename: env.d.ts
declare module "sst" {
  export interface Resource { A: { value: string } }
}
import "sst"
export {}
// @filename: stats.d.ts
import "sst/resource"
declare module "sst/resource" {
  export interface Resource { B: { value: string } }
}
// @filename: use.ts
import { R } from "m";
import { R as RA } from "m/a";
import { R as RB } from "m/b";
import { Resource } from "sst";
R.base; R.a; R.b; R.c;
declare const t: R; t.a; t.b; t.c;
declare const ta: RA; ta.a; ta.b; ta.c;
declare const tb: RB; tb.a; tb.b; tb.c;
RA.a; RA.b; RA.c; RB.a; RB.b; RB.c;
Resource.A.value;
Resource.B.value;
Resource.App.name;
declare const res: Resource;
res.A.value; res.B.value; res.App;
