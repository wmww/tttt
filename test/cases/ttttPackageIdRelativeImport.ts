// @noImplicitReferences: true

// A file reached by a relative import has no package id (tsgo sets one only on node_modules package lookups and
// exports/imports maps): two copies of the same package version stay two files, their private members conflict (bun's
// 36-C1)
// @filename: /a.ts
import { C as A } from "./node_modules/pa";
import { C as B } from "./node_modules/dep/node_modules/pa";
declare const b: B;
const y: A = b;
// @filename: /node_modules/dep/node_modules/pa/index.d.ts
export declare class C { private x: number; }
// @filename: /node_modules/dep/node_modules/pa/package.json
{"name":"pa","version":"1.0.0"}
// @filename: /node_modules/pa/index.d.ts
export declare class C { private x: number; }
// @filename: /node_modules/pa/package.json
{"name":"pa","version":"1.0.0"}
// @filename: /tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
