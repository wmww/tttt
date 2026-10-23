// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { which } from "cs1";
const x: 0 = which;
const sentinel: number = "";
// @filename: node_modules/cs1/index.d.ts
export declare const which: "index";
// @filename: node_modules/cs1/package.json
{"name":"cs1","version":"1.0.0","types":"../CS1/index.d.ts","typesVersions":{"*":{"*":["ts/*"]}}}
// @filename: node_modules/cs1/ts/index.d.ts
export declare const which: "ts/index";
