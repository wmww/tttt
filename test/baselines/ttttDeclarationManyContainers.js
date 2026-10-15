//// [test/cases/ttttDeclarationManyContainers.ts] ////

//// [types.ts]
export interface Logger { log(message: string): void }
export declare function make(): Logger;
//// [r1.ts]
export * from "./types";
//// [r2.ts]
export * from "./types";
//// [r3.ts]
export * from "./types";
//// [r4.ts]
export * from "./types";
//// [r5.ts]
export * from "./types";
//// [r6.ts]
export * from "./types";
//// [r7.ts]
export * from "./types";
//// [r8.ts]
export * from "./types";
//// [r9.ts]
export * from "./types";
//// [r10.ts]
export * from "./types";
//// [index.ts]
export * from "./lib/deep/types";
//// [x.ts]
import { make } from "../lib/deep/types";
export const logger = make();


//// [types.js]
export {};
//// [r1.js]
export * from "./types";
//// [r2.js]
export * from "./types";
//// [r3.js]
export * from "./types";
//// [r4.js]
export * from "./types";
//// [r5.js]
export * from "./types";
//// [r6.js]
export * from "./types";
//// [r7.js]
export * from "./types";
//// [r8.js]
export * from "./types";
//// [r9.js]
export * from "./types";
//// [r10.js]
export * from "./types";
//// [index.js]
export * from "./lib/deep/types";
//// [x.js]
import { make } from "../lib/deep/types";
export const logger = make();


//// [types.d.ts]
export interface Logger {
    log(message: string): void;
}
export declare function make(): Logger;
//// [r1.d.ts]
export * from "./types";
//// [r2.d.ts]
export * from "./types";
//// [r3.d.ts]
export * from "./types";
//// [r4.d.ts]
export * from "./types";
//// [r5.d.ts]
export * from "./types";
//// [r6.d.ts]
export * from "./types";
//// [r7.d.ts]
export * from "./types";
//// [r8.d.ts]
export * from "./types";
//// [r9.d.ts]
export * from "./types";
//// [r10.d.ts]
export * from "./types";
//// [index.d.ts]
export * from "./lib/deep/types";
//// [x.d.ts]
export declare const logger: import("..").Logger;
