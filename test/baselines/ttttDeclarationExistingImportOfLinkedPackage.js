//// [test/cases/ttttDeclarationExistingImportOfLinkedPackage.ts] ////

//// [package.json]
{ "name": "lib", "version": "1.0.0", "types": "index.d.ts" }
//// [index.d.ts]
export interface Opts { a: number }
export declare function make(): Opts;
//// [a.ts]
import { make } from "lib/index";
export const x = make();


//// [a.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.x = void 0;
const index_1 = require("lib/index");
exports.x = (0, index_1.make)();


//// [a.d.ts]
export declare const x: import("lib").Opts;
