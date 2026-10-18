// The second copy of a package (same name and version) is a redirect to the first, and what only the redirect refers
// to isn't part of the program at all (tsgo's collectFiles doesn't walk a redirect's imports): b's copy's own global
// file is never seen, a later import of it by path brings it in on its own. Which copy is the first is decided once
// everything is loaded, in program order, not by which resolution with the package id came first
// @module: commonjs
// @filename: /tsconfig.json
{ "compilerOptions": { "module": "commonjs", "strict": true }, "files": ["main.ts"] }
// @filename: /a/node_modules/pkg/package.json
{ "name": "pkg", "version": "1.0.0", "types": "index.d.ts" }
// @filename: /a/node_modules/pkg/index.d.ts
export declare class C { private p; }
// @filename: /a/index.ts
export { C } from "pkg";
// @filename: /b/node_modules/pkg/package.json
{ "name": "pkg", "version": "1.0.0", "types": "index.d.ts" }
// @filename: /b/node_modules/pkg/index.d.ts
import "./globals";
export declare class C { private p; }
// @filename: /b/node_modules/pkg/globals.d.ts
declare var onlySecondCopy: number;
declare var onlySecondCopyToo: number;
// @filename: /b/index.ts
export { C } from "pkg";
export const n: number = onlySecondCopy;
// @filename: /main.ts
import * as a from "./a";
import * as b from "./b";
declare const c: a.C;
export const c2: b.C = c;
export const m: number = onlySecondCopyToo;
