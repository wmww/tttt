//// [test/cases/ttttDeclarationExportsConditionByTargetFormat.ts] ////

//// [package.json]
{ "name": "lib", "version": "1.0.0", "exports": { ".": { "types": { "import": "./dist/index.d.ts", "require": "./dist/index.d.cts" }, "default": "./dist/index.js" } } }
//// [index.d.cts]
export interface Options { a: number }
export declare function make(): Options;
//// [index.d.ts]
export interface Options { a: number }
export declare function make(): Options;
//// [b.ts]
import lib = require("lib");
export const make = lib.make;
//// [a.ts]
import { make } from "./b";
export const v = make();


//// [b.js]
const lib = require("lib");
export const make = lib.make;
//// [a.js]
import { make } from "./b";
export const v = make();


//// [b.d.ts]
import lib = require("lib");
export declare const make: typeof lib.make;
//// [a.d.ts]
export declare const v: import("lib").Options;
