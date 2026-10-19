// `'x' in C` narrows an imported class: tsc's checkIdentifier narrows an alias like a variable (assumed initialized),
// not a class declared in the file (angular-cli's `if ('_pathCache' in Module) Module._pathCache = ...` of node:module)
// @strict: true
// @filename: /m.ts
export declare class Module { static x: number }
// @filename: /a.ts
import { Module } from "./m";
declare class Local { static x: number }
if ('_pathCache' in Module) {
    Module._pathCache = Object.create(null);
}
if ('_pathCache' in Local) {
    Local._pathCache = 1;
}
