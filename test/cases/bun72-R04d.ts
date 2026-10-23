// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler"}}
// @filename: a.ts
import { T } from "./node_modules/third";
import "old";
import "dep";
import { take3 } from "dep3";
take3(new T());
const sentinel: number = "";
// @filename: node_modules/dep/index.d.ts
import "old";
export {};
// @filename: node_modules/dep/node_modules/old/index.d.ts
import "third";
export declare class C { private p; }
// @filename: node_modules/dep/node_modules/old/package.json
{ "name": "old", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/dep/package.json
{ "name": "dep", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/dep3/index.d.ts
import { T } from "third";
export declare function take3(t: T): void;
// @filename: node_modules/dep3/node_modules/third/index.d.ts
export declare class T { private t; }
// @filename: node_modules/dep3/node_modules/third/package.json
{ "name": "third", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/dep3/package.json
{ "name": "dep3", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/old/index.d.ts
export declare class C { private p; }
// @filename: node_modules/old/package.json
{ "name": "old", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/third/index.d.ts
export declare class T { private t; }
// @filename: node_modules/third/package.json
{ "name": "third", "version": "1.0.0", "types": "index.d.ts" }
