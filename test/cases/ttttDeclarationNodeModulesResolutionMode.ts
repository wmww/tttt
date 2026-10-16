// @declaration: true
// @strict: true
// @module: node16
// @noTypesAndSymbols: true
// An import type of an ES module from a CommonJS file has a `resolution-mode` attribute, and then it is never TS2883,
// even with a specifier into node_modules (tsgo's symbolToTypeNode checks importModeOverride, which the attribute
// set; sequelize's model-typescript.ts)

// @filename: /node_modules/pkg/package.json
{ "name": "pkg", "type": "module", "exports": { ".": "./index.js" } }

// @filename: /node_modules/pkg/index.d.ts
import { Internal } from "./internal.js";
export declare function make(): Internal;

// @filename: /node_modules/pkg/internal.d.ts
export interface Internal { a: number }

// @filename: /package.json
{ "name": "root" }

// @filename: /src/a.cts
import type * as P from "pkg" with { "resolution-mode": "import" };
declare const m: typeof P;
export const v = m.make();
