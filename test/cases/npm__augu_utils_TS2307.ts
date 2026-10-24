// origin: npm @augu/utils@1.5.6 https://github.com/auguwu/utils tsconfig.json seed 20261200
// flip: no
// @module: commonjs
// @strict: true
// @noEmit: true
// A package importing its own name (package.json name + exports) whose exports target does not exist in the project
// (dist/ is not built): tsgo's self-name lookup continues searching into node_modules (a published copy of the same
// package) and finds it there; tttt stops at the self-name and reports TS2307.
// @filename: package.json
{"name": "@noelware/utils", "version": "2.5.1", "exports": {".": "./dist/index.js"}}
// @filename: node_modules/@noelware/utils/package.json
{"name": "@noelware/utils", "version": "2.5.1", "exports": {".": "./dist/index.js"}}
// @filename: node_modules/@noelware/utils/dist/index.d.ts
export declare function hasOwnProperty(o: object, key: string): boolean;
// @filename: scripts/util/logging.ts
import { hasOwnProperty } from "@noelware/utils";
export const y: string = hasOwnProperty({}, "a");
