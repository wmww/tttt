// @noImplicitReferences: true

// A subpath with its own package.json takes its package id from that one (tsgo's getPackageId with the nested
// packageInfo): without a name or version, none, so two copies don't dedupe (bun's 36-C1d)
// @filename: /a.ts
import { C } from "pd/sub";
import { c } from "dep";
const y: C = c;
// @filename: /node_modules/dep/index.d.ts
import { C } from "pd/sub";
export declare const c: C;
// @filename: /node_modules/dep/node_modules/pd/dist/sub.d.ts
export declare class C { private x: number; }
// @filename: /node_modules/dep/node_modules/pd/package.json
{"name":"pd","version":"1.0.0"}
// @filename: /node_modules/dep/node_modules/pd/sub/package.json
{"types":"../dist/sub.d.ts"}
// @filename: /node_modules/pd/dist/sub.d.ts
export declare class C { private x: number; }
// @filename: /node_modules/pd/package.json
{"name":"pd","version":"1.0.0"}
// @filename: /node_modules/pd/sub/package.json
{"types":"../dist/sub.d.ts"}
// @filename: /tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
