// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "module": "esnext", "moduleResolution": "bundler", "preserveSymlinks": true}}
// @filename: a.ts
import { b0, m0 } from "p0";
import { b1, m1 } from "p1";
import { b2, m2 } from "p2";
let b = b0; b = b1; b = b2;
let m = m0; m = m1; m = m2;
{ const probe: never = b0.d; }
{ const probe: never = m0; }
{ const probe: never = b1.d; }
{ const probe: never = m1; }
{ const probe: never = b2.d; }
{ const probe: never = m2; }
declare const g0: FromDep0; declare const g1: FromDep1; declare const g2: FromDep2;
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
export type D = 0;
declare global { interface FromDep0 { k: 0 } }
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
declare global { interface FromDep1 { k: 1 } }
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
export type D = 2;
declare global { interface FromDep2 { k: 2 } }
// @filename: node_modules/p2/node_modules/dep/package.json
{"name": "dep", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/p2/package.json
{"name": "p2", "version": "1.0.0", "types": "index.d.ts"}
