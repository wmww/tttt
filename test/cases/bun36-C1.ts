// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { C as A } from "./node_modules/pa";
import { C as B } from "./node_modules/dep/node_modules/pa";
declare const b: B;
const y: A = b;
const sentinel: number = "";
// @filename: node_modules/dep/node_modules/pa/index.d.ts
export declare class C { private x: number; }
// @filename: node_modules/dep/node_modules/pa/package.json
{"name":"pa","version":"1.0.0"}
// @filename: node_modules/pa/index.d.ts
export declare class C { private x: number; }
// @filename: node_modules/pa/package.json
{"name":"pa","version":"1.0.0"}
