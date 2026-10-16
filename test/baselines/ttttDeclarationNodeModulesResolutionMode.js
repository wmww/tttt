//// [test/cases/ttttDeclarationNodeModulesResolutionMode.ts] ////

//// [package.json]
{ "name": "pkg", "type": "module", "exports": { ".": "./index.js" } }

//// [index.d.ts]
import { Internal } from "./internal.js";
export declare function make(): Internal;

//// [internal.d.ts]
export interface Internal { a: number }

//// [package.json]
{ "name": "root" }

//// [a.cts]
import type * as P from "pkg" with { "resolution-mode": "import" };
declare const m: typeof P;
export const v = m.make();


//// [a.cjs]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.v = void 0;
exports.v = m.make();


//// [a.d.cts]
export declare const v: import("../node_modules/pkg/internal.js", { with: { "resolution-mode": "import" } }).Internal;
