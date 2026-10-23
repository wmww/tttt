// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { which } from "v";
const x: 0 = which;
const sentinel: number = "";
// @filename: node_modules/v/index.d.ts
export declare const which: "index";
// @filename: node_modules/v/package.json
{"name":"v","version":"1.0.0","typesVersions":{">= 4.0":{"*":["t.d.ts"]}}}
// @filename: node_modules/v/t.d.ts
export declare const which: "t";
