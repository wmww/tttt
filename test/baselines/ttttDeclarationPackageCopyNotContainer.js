//// [test/cases/ttttDeclarationPackageCopyNotContainer.ts] ////

//// [package.json]
{ "name": "next", "version": "1.0.0", "types": "index.d.ts" }
//// [index.d.ts]
export interface Handler { x: number }
//// [package.json]
{ "name": "next", "version": "1.0.0", "types": "index.d.ts" }
//// [index.d.ts]
export interface Handler { x: number }
//// [package.json]
{ "name": "lib2", "version": "1.0.0" }
//// [sub.d.ts]
import type { Handler } from "next";
export type { Handler } from "next";
export declare function handler(): Handler;
//// [main.ts]
import type { Handler } from "next";
export const h: Handler | undefined = undefined;
//// [y.ts]
import { handler } from "lib2/sub";
export default handler();


//// [main.js]
export const h = undefined;
//// [y.js]
import { handler } from "lib2/sub";
export default handler();


//// [main.d.ts]
import type { Handler } from "next";
export declare const h: Handler | undefined;
//// [y.d.ts]
declare const _default: import("lib2/sub").Handler;
export default _default;
