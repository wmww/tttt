// The same with direct re-exports: the second path's alias resolves to the interface as declared, a clone of its own,
// so only the two augmentations through `start` conflict (TS2717 in manager1)
// @strict: true
// @module: esnext
// @moduleResolution: bundler
// @noTypesAndSymbols: true
// @filename: node_modules/core/index.d.ts
export interface Register {}
export type Registered = Register extends { router: infer R } ? R : "none";
// @filename: node_modules/core/package.json
{ "name": "core", "types": "index.d.ts" }
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
