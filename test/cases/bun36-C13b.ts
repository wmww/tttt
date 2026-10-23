// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { C } from "en";
import { c } from "dep";
const y: C = c;
const sentinel: number = "";
// @filename: node_modules/dep/index.d.ts
import { C } from "en";
export declare const c: C;
// @filename: node_modules/dep/node_modules/en/index.d.ts
export declare class C { private x: number; }
// @filename: node_modules/dep/node_modules/en/package.json
{"name":"","version":"1.0.0"}
// @filename: node_modules/en/index.d.ts
export declare class C { private x: number; }
// @filename: node_modules/en/package.json
{"name":"","version":"1.0.0"}
