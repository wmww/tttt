// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "module": "esnext", "moduleResolution": "bundler"}}
// @filename: a.ts
import { b2 } from "p2";
import { b1 } from "p1";
import { b0 } from "p0";
let b = b2; b = b1; b = b0;
{ const probe: never = b0; }
const sentinel: number = "";
// @filename: node_modules/p0/index.d.ts
import { Big, M } from "big";
export declare const b0: Big;
export declare const m0: M;
// @filename: node_modules/p0/node_modules/big/index.d.ts
import type { D } from "dep";
export * from "./m";
export declare class Big { #secret: number; d: D; }
// @filename: node_modules/p0/node_modules/big/m.d.ts
export interface M { m: number }
// @filename: node_modules/p0/node_modules/big/package.json
{"name": "big", "version": "1.2.3", "types": "index.d.ts"}
// @filename: node_modules/p0/node_modules/dep/index.d.ts
export type D = 1;
// @filename: node_modules/p0/node_modules/dep/package.json
{"name": "dep", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/p0/package.json
{"name": "p0", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/p1/index.d.ts
import { Big, M } from "big";
export declare const b1: Big;
export declare const m1: M;
// @filename: node_modules/p1/node_modules/big/index.d.ts
import type { D } from "dep";
export * from "./m";
export declare class Big { #secret: number; d: D; }
// @filename: node_modules/p1/node_modules/big/m.d.ts
export interface M { m: number }
// @filename: node_modules/p1/node_modules/big/package.json
{"name": "big", "version": "1.2.3", "types": "index.d.ts"}
// @filename: node_modules/p1/node_modules/dep/index.d.ts
export type D = 1;
// @filename: node_modules/p1/node_modules/dep/package.json
{"name": "dep", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/p1/package.json
{"name": "p1", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/p2/index.d.ts
import { Big, M } from "big";
export declare const b2: Big;
export declare const m2: M;
// @filename: node_modules/p2/node_modules/big/index.d.ts
import type { D } from "dep";
export * from "./m";
export declare class Big { #secret: number; d: D; }
// @filename: node_modules/p2/node_modules/big/m.d.ts
export interface M { m: number }
// @filename: node_modules/p2/node_modules/big/package.json
{"name": "big", "version": "1.2.3", "types": "index.d.ts"}
// @filename: node_modules/p2/node_modules/dep/index.d.ts
export type D = 1;
// @filename: node_modules/p2/node_modules/dep/package.json
{"name": "dep", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/p2/package.json
{"name": "p2", "version": "1.0.0", "types": "index.d.ts"}
