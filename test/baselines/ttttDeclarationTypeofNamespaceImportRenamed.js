//// [test/cases/ttttDeclarationTypeofNamespaceImportRenamed.ts] ////

//// [cc.ts]
export const a = 1;
export const b = 2;
export const c = 3;
//// [a.ts]
import * as cc from "./cc";
export class A {
    f(x: (typeof cc)[keyof typeof cc]): void {}
    g(y: keyof typeof cc, z: typeof cc, w: typeof cc.b): void {}
}
//// [b.ts]
import { A } from "./a";
export const v = new A().f;
export const w = new A().g;


//// [cc.js]
export const a = 1;
export const b = 2;
export const c = 3;
//// [a.js]
export class A {
    f(x) { }
    g(y, z, w) { }
}
//// [b.js]
import { A } from "./a";
export const v = new A().f;
export const w = new A().g;


//// [cc.d.ts]
export declare const a = 1;
export declare const b = 2;
export declare const c = 3;
//// [a.d.ts]
import * as cc from "./cc";
export declare class A {
    f(x: (typeof cc)[keyof typeof cc]): void;
    g(y: keyof typeof cc, z: typeof cc, w: typeof cc.b): void;
}
//// [b.d.ts]
export declare const v: (x: (typeof import("./cc"))[keyof typeof import("./cc")]) => void;
export declare const w: (y: keyof typeof import("./cc"), z: typeof import("./cc"), w: typeof import("./cc").b) => void;
