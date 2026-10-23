// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { which } from "p";
const x: 0 = which;
const sentinel: number = "";
// @filename: node_modules/p/index.d.ts
export declare const which: "index";
// @filename: node_modules/p/package.json
{"name":"p","version":"1.0.0","types":".","typesVersions":{"*":{"*":["ts/*"]}}}
// @filename: node_modules/p/ts/index.d.ts
export declare const which: "ts/index";
