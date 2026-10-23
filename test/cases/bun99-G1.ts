// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["esnext"], "module": "commonjs", "skipLibCheck": true}, "files": ["n.d.ts", "o.d.ts", "case.ts"]}
// @filename: case.ts
export type A = globalThis.ErrorConstructor;
export type B = globalThis.Console;
export type C = globalThis.console.Options;
export const d: never = globalThis.console;
import c = require("console");
export const e: never = c;
export type F = c.Console;
export type G = c.Options;
export type H = globalThis.Later;
export type I = globalThis.LaterNs.T;
export const j: never = globalThis.later;
export interface Extended extends globalThis.Later { z?: 1 }
export const k: Extended = { l: 1 };
type Return = ReturnType<any[][typeof Symbol.iterator]> extends globalThis.Iterator<any, infer R> ? R : any;
export const probe: [Return] extends [undefined] ? true : false = true;
export type L = globalThis.NoSuchType;
const sentinel: number = "";
// @filename: n.d.ts
declare module "console" {
    import console = require("node:console");
    export = console;
}
declare module "node:console" {
    global {
        interface Console { log(): void }
        namespace console { interface Options { a?: 1 } }
        var console: Console;
    }
    export = globalThis.console;
}
// @filename: o.d.ts
export {};
declare module "console" {
  interface Console { extra(): void }
}
declare global { interface Later { l: 1 } var later: Later; namespace LaterNs { type T = 1 } }
