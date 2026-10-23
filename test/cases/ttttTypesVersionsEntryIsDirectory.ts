// @strict: true
// @module: esnext
// @moduleResolution: bundler
// @noTypesAndSymbols: true
// A package whose `types` is its own directory: typesVersions maps the empty relative name (bun's 36-C8 aborted)
// @filename: node_modules/p/index.d.ts
export declare const which: "index";
// @filename: node_modules/p/package.json
{"name":"p","version":"1.0.0","types":".","typesVersions":{"*":{"*":["ts/*"]}}}
// @filename: node_modules/p/ts/index.d.ts
export declare const which: "ts/index";
// @filename: a.ts
import { which } from "p";
const x: 0 = which;
