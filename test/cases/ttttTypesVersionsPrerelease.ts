// typesVersions ranges are tested against tsgo's version 7.1.0-beta with tsgo's semver rules: a prerelease, so
// `>=7.1.0` and `^7.1.0` don't hold, `>=7.1`, `7.x`, `<7.1.0` and `7.1.0-alpha - 7.2` do (bun-types 1.4's
// `{ ">=7.1": { "index.d.ts": ["ts7.1/index.d.ts"] } }`, which declares its import-attribute modules: emailthing,
// source sweep refresh 5). tttt tested them against 6.0
// @noTypesAndSymbols: true
// @module: esnext
// @moduleResolution: bundler
// @filename: /node_modules/a/package.json
{ "name": "a", "types": "index.d.ts", "typesVersions": { ">=7.1.0": { "*": ["v710/*"] }, "^7.1.0": { "*": ["v710/*"] }, ">=7.1": { "*": ["v71/*"] } } }
// @filename: /node_modules/a/index.d.ts
export declare const which: "none";
// @filename: /node_modules/a/v710/index.d.ts
export declare const which: "7.1.0";
// @filename: /node_modules/a/v71/index.d.ts
export declare const which: "7.1";
// @filename: /node_modules/b/package.json
{ "name": "b", "types": "index.d.ts", "typesVersions": { "<7.1.0": { "*": ["pre/*"] } } }
// @filename: /node_modules/b/index.d.ts
export declare const which: "none";
// @filename: /node_modules/b/pre/index.d.ts
export declare const which: "pre";
// @filename: /node_modules/c/package.json
{ "name": "c", "types": "index.d.ts", "typesVersions": { "7.1.0-alpha - 7.2": { "*": ["r/*"] } } }
// @filename: /node_modules/c/index.d.ts
export declare const which: "none";
// @filename: /node_modules/c/r/index.d.ts
export declare const which: "range";
// @filename: /node_modules/d/package.json
{ "name": "d", "types": "index.d.ts", "typesVersions": { "<7.1": { "*": ["old/*"] }, "6.x || 7.x": { "*": ["x/*"] } } }
// @filename: /node_modules/d/index.d.ts
export declare const which: "none";
// @filename: /node_modules/d/x/index.d.ts
export declare const which: "x";
// @filename: /node_modules/d/old/index.d.ts
export declare const which: "old";
// @filename: /main.ts
import { which as a } from "a";
import { which as b } from "b";
import { which as c } from "c";
import { which as d } from "d";
export const x: [1, 1, 1, 1] = [a, b, c, d];
