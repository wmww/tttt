//// [test/cases/ttttDeclarationUnsafeImportModuleName.ts] ////

//// [index.d.ts]
declare const v: { a: 1 };
export default v;
//// [other.d.ts]
declare const w: { b: 1 };
export default w;
//// [index.d.ts]
export declare function load(): Promise<typeof import("nested")>;
export declare function loadOther(): Promise<typeof import("nested/other")>;
//// [entry.ts]
import { load, loadOther } from "foo";
export const x = load();
export const y = [load(), loadOther()];


//// [entry.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.y = exports.x = void 0;
const foo_1 = require("foo");
exports.x = (0, foo_1.load)();
exports.y = [(0, foo_1.load)(), (0, foo_1.loadOther)()];
