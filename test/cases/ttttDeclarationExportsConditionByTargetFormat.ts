// tsgo matches a package's `exports` conditions by the target file's own format (`.d.cts`: require, `.d.mts`:
// import) when naming it, where tsc took the importing file's mode: a CommonJS typing reached through `require` is
// still the package's name (docmost's editor-ext: TS2883 with a relative path into node_modules)
// @declaration: true
// @module: preserve
// @moduleResolution: bundler
// @filename: /node_modules/.pnpm/lib@1.0.0/node_modules/lib/package.json
{ "name": "lib", "version": "1.0.0", "exports": { ".": { "types": { "import": "./dist/index.d.ts", "require": "./dist/index.d.cts" }, "default": "./dist/index.js" } } }
// @filename: /node_modules/.pnpm/lib@1.0.0/node_modules/lib/dist/index.d.cts
export interface Options { a: number }
export declare function make(): Options;
// @filename: /node_modules/.pnpm/lib@1.0.0/node_modules/lib/dist/index.d.ts
export interface Options { a: number }
export declare function make(): Options;
// @filename: /b.ts
import lib = require("lib");
export const make = lib.make;
// @filename: /a.ts
import { make } from "./b";
export const v = make();
// @link: /node_modules/.pnpm/lib@1.0.0/node_modules/lib -> /node_modules/lib
