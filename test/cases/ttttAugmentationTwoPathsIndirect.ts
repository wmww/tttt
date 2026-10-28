// origin: thirdparty/sources/appflare (root), apps/manager/src/router.tsx: tanstack's Register augmented through
// react-start's `export *` (docs) and react-router's re-export of router-core's re-export (manager)
// Two augmentations of one interface through two paths: the second clones the first's clone when its alias is
// indirect (tsc's resolveIndirectionAlias: getMergedSymbol), TS2717 there; each clone's properties are checked
// @strict: true
// @module: esnext
// @moduleResolution: bundler
// @noTypesAndSymbols: true
// @filename: node_modules/core/index.d.ts
export type { Register } from "./reg";
import type { Register } from "./reg";
export type Registered = Register extends { router: infer R } ? R : "none";
// @filename: node_modules/core/package.json
{ "name": "core", "types": "index.d.ts" }
// @filename: node_modules/core/reg.d.ts
export interface Register {}
// @filename: node_modules/router/index.d.ts
export type { Register, Registered } from "core";
// @filename: node_modules/router/package.json
{ "name": "router", "types": "index.d.ts" }
// @filename: node_modules/start-core/index.d.ts
export type { Register, Registered } from "core";
// @filename: node_modules/start-core/package.json
{ "name": "start-core", "types": "index.d.ts" }
// @filename: node_modules/start/index.d.ts
export * from "start-core";
// @filename: node_modules/start/package.json
{ "name": "start", "types": "index.d.ts" }
// @filename: docs.ts
import type { Registered } from "start";
declare module "start" {
  interface Register { router: { docs: true } }
}
export const d: Registered = { docs: true };
// @filename: manager1.ts
declare module "start" {
  interface Register { router: { manager: true } }
}
export {};
// @filename: manager.ts
import type { Registered } from "router";
declare module "router" {
  interface Register { router: { manager: true } }
}
export const m: Registered = { manager: true };
