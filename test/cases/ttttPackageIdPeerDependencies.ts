// Two installs of one package version are one package to deduplication (tsc's packageIdToSourceFile) only if their
// peer dependencies are the same versions too (tsgo's readPackageJsonPeerDependencies: the package id has
// `+peer@version` for each peer next to the package): pnpm's `vite@8.3.0_esbuild@0.27.7` and `..._esbuild@0.28.2`
// directories are two vites (trpc/examples/nuxt's TS2416 in vite's own .d.ts depends on it)
// @module: commonjs
// @filename: /a/node_modules/pkg/package.json
{ "name": "pkg", "version": "1.0.0", "types": "index.d.ts", "peerDependencies": { "peer": "*", "absent": "*" } }
// @filename: /a/node_modules/pkg/index.d.ts
export declare class C { private p; }
// @filename: /a/node_modules/peer/package.json
{ "name": "peer", "version": "1.0.0" }
// @filename: /a/node_modules/same/package.json
{ "name": "same", "version": "1.0.0", "types": "index.d.ts", "peerDependencies": { "peer2": "*" } }
// @filename: /a/node_modules/same/index.d.ts
export declare class D { private p; }
// @filename: /a/node_modules/peer2/package.json
{ "name": "peer2", "version": "1.0.0" }
// @filename: /a/index.ts
export { C } from "pkg";
export { D } from "same";
// @filename: /b/node_modules/pkg/package.json
{ "name": "pkg", "version": "1.0.0", "types": "index.d.ts", "peerDependencies": { "peer": "*", "absent": "*" } }
// @filename: /b/node_modules/pkg/index.d.ts
export declare class C { private p; }
// @filename: /b/node_modules/peer/package.json
{ "name": "peer", "version": "2.0.0" }
// @filename: /b/node_modules/same/package.json
{ "name": "same", "version": "1.0.0", "types": "index.d.ts", "peerDependencies": { "peer2": "*" } }
// @filename: /b/node_modules/same/index.d.ts
export declare class D { private p; }
// @filename: /b/node_modules/peer2/package.json
{ "name": "peer2", "version": "1.0.0" }
// @filename: /b/index.ts
export { C } from "pkg";
export { D } from "same";
// @filename: /main.ts
import * as a from "./a";
import * as b from "./b";
declare const c: a.C;
export const c2: b.C = c;
declare const d: a.D;
export const d2: b.D = d;
