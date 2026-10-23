// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "declaration": true, "emitDeclarationOnly": true, "outDir": "out", "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { make, make2, make3 } from "other";
export const x = make();
export const x2 = make2();
export const x3 = make3();
const sentinel: number = "";
// @filename: node_modules/dep/a.d.ts
export interface Thing { a: number }
// @filename: node_modules/dep/package.json
{ "name": "dep", "version": "1.0.0", "exports": { "./a": "./a.d.ts", "#x": "./x.d.ts" } }
// @filename: node_modules/dep2/a.d.ts
export interface Thing2 { a: number }
// @filename: node_modules/dep2/package.json
{ "name": "dep2", "version": "1.0.0", "exports": { "": "./x.d.ts", "./a": "./a.d.ts" } }
// @filename: node_modules/dep3/a.d.ts
export interface Thing3 { a: number }
// @filename: node_modules/dep3/package.json
{ "name": "dep3", "version": "1.0.0", "exports": { "./a": "./a.d.ts" } }
// @filename: node_modules/other/index.d.ts
import { Thing } from "dep/a";
import { Thing2 } from "dep2/a";
import { Thing3 } from "dep3/a";
export declare function make(): Thing;
export declare function make2(): Thing2;
export declare function make3(): Thing3;
// @filename: node_modules/other/package.json
{ "name": "other", "version": "1.0.0", "types": "index.d.ts" }
// @filename: out/a.d.ts
export declare const x: import("dep/a").Thing;
export declare const x2: import("dep2/a").Thing2;
export declare const x3: import("dep3/a").Thing3;
