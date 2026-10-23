// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { which } from "p/x";
const x: 0 = which;
const sentinel: number = "";
// @filename: node_modules/p/package.json
{"name":"p","version":"1.0.0","typesVersions":{"*":{"x":"notarray","*":["ts/*"]}}}
// @filename: node_modules/p/ts/x.d.ts
export declare const which: "ts/x";
// @filename: node_modules/p/x.d.ts
export declare const which: "x";
