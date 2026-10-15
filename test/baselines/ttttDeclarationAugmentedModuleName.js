//// [test/cases/ttttDeclarationAugmentedModuleName.ts] ////

//// [package.json]
{ "name": "vitest", "version": "1.0.0", "types": "index.d.ts" }
//// [package.json]
{ "name": "vite", "version": "1.0.0", "types": "index.d.ts" }
//// [index.d.ts]
export interface UserConfig { root?: string }
export interface Other { x: number }
//// [index.d.ts]
import { UserConfig } from "vite";
export { UserConfig as ViteUserConfig, Other } from "vite";
declare module "vite" {
    interface UserConfig { test?: { include?: string[] } }
}
export declare function defineConfig(config: UserConfig): UserConfig;
export declare function other(): import("vite").Other;
//// [a.ts]
import { defineConfig, other } from "vitest";
export default defineConfig({ test: { include: [] } });
export const o = other();


//// [a.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.o = void 0;
const vitest_1 = require("vitest");
exports.default = (0, vitest_1.defineConfig)({ test: { include: [] } });
exports.o = (0, vitest_1.other)();


//// [a.d.ts]
declare const _default: import("vite").UserConfig;
export default _default;
export declare const o: import("vite").Other;


//// [DtsFileErrors]


/a.d.ts(1,32): error TS2307: Cannot find module 'vite' or its corresponding type declarations.
/a.d.ts(3,32): error TS2307: Cannot find module 'vite' or its corresponding type declarations.


==== /node_modules/vitest/package.json (0 errors) ====
    { "name": "vitest", "version": "1.0.0", "types": "index.d.ts" }
==== /node_modules/vitest/node_modules/vite/package.json (0 errors) ====
    { "name": "vite", "version": "1.0.0", "types": "index.d.ts" }
==== /node_modules/vitest/node_modules/vite/index.d.ts (0 errors) ====
    export interface UserConfig { root?: string }
    export interface Other { x: number }
==== /node_modules/vitest/index.d.ts (0 errors) ====
    import { UserConfig } from "vite";
    export { UserConfig as ViteUserConfig, Other } from "vite";
    declare module "vite" {
        interface UserConfig { test?: { include?: string[] } }
    }
    export declare function defineConfig(config: UserConfig): UserConfig;
    export declare function other(): import("vite").Other;
==== /a.d.ts (2 errors) ====
    declare const _default: import("vite").UserConfig;
                                   ~~~~~~
!!! error TS2307: Cannot find module 'vite' or its corresponding type declarations.
    export default _default;
    export declare const o: import("vite").Other;
                                   ~~~~~~
!!! error TS2307: Cannot find module 'vite' or its corresponding type declarations.
    