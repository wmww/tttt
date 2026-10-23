// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { which } from "p/x";
const x: 0 = which;
const sentinel: number = "";
// @filename: node_modules/p/index.d.ts
export declare const which: "index";
// @filename: node_modules/p/package.json
{"name":"p","version":"1.0.0","typesVersions":{"*":{"*":[42]}}}
// @filename: node_modules/p/x.d.ts
export declare const which: "x";
