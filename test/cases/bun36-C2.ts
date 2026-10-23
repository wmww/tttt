// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { C } from "pd/sub";
import { c } from "dep";
const y: C = c;
const sentinel: number = "";
// @filename: node_modules/dep/index.d.ts
import { C } from "pd/sub";
export declare const c: C;
// @filename: node_modules/dep/node_modules/pd/package.json
{"name":"pd","version":"1.0.1"}
// @filename: node_modules/dep/node_modules/pd/sub/index.d.ts
export declare class C { private x: number; }
// @filename: node_modules/dep/node_modules/pd/sub/package.json
{"name":"pdsub","version":"2.0.0"}
// @filename: node_modules/dep/package.json
{"name":"dep","version":"1.0.0"}
// @filename: node_modules/pd/package.json
{"name":"pd","version":"1.0.0"}
// @filename: node_modules/pd/sub/index.d.ts
export declare class C { private x: number; }
// @filename: node_modules/pd/sub/package.json
{"name":"pdsub","version":"2.0.0"}
