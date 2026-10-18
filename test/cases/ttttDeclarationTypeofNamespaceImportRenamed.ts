// @declaration: true
// @strict: true
// A reused `typeof` query whose name isn't accessible where it's printed (a namespace import of another file) is
// named anew by tsc's serializeTypeName, the whole name resolved: `typeof import("./cc")` for the module (accessible
// anywhere through an import type), `typeof import("./cc").b` for a qualified name, so the `keyof` and indexed access
// around it are reused instead of printing their types (babel-parser's `(typeof charCodes)[keyof typeof charCodes]`).
// @filename: cc.ts
export const a = 1;
export const b = 2;
export const c = 3;
// @filename: a.ts
import * as cc from "./cc";
export class A {
    f(x: (typeof cc)[keyof typeof cc]): void {}
    g(y: keyof typeof cc, z: typeof cc, w: typeof cc.b): void {}
}
// @filename: b.ts
import { A } from "./a";
export const v = new A().f;
export const w = new A().g;
