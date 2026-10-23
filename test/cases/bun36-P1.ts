// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { C } from "pp";
import { c } from "dep";
const y: C = c;
const sentinel: number = "";
// @filename: node_modules/dep/index.d.ts
import { C } from "pp";
export declare const c: C;
// @filename: node_modules/dep/node_modules/peer/package.json
{"name":"peer","version":"2.0.0"}
// @filename: node_modules/dep/node_modules/pp/index.d.ts
export declare class C { private x: number; }
// @filename: node_modules/dep/node_modules/pp/package.json
{"name":"pp","version":"1.0.0","peerDependencies":{"peer":null}}
// @filename: node_modules/peer/package.json
{"name":"peer","version":"1.0.0"}
// @filename: node_modules/pp/index.d.ts
export declare class C { private x: number; }
// @filename: node_modules/pp/package.json
{"name":"pp","version":"1.0.0","peerDependencies":{"peer":null}}
