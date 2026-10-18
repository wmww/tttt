// TS1541/TS1542 (a type-only import or import type of an ES module from a CommonJS file) carry the same "To convert
// this file to an ECMAScript module" detail as TS1479
// @module: node16
// @filename: /node_modules/esm/package.json
{ "name": "esm", "type": "module", "exports": "./index.js" }
// @filename: /node_modules/esm/index.d.ts
export interface T { x: number }
// @filename: /package.json
{ "name": "app" }
// @filename: /a.ts
import type { T } from "esm";
type U = import("esm").T;
export const t: T | U = { x: 1 };
