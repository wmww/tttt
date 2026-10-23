// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { which } from "q/sub";
const x: 0 = which;
const sentinel: number = "";
// @filename: node_modules/q/package.json
{"name":"q","version":"1.0.0","typesVersions":{"*":{"sub":["ts/"]}}}
// @filename: node_modules/q/ts.d.ts
export declare const which: "ts.d.ts";
// @filename: node_modules/q/ts/index.d.ts
export declare const which: "ts/index";
