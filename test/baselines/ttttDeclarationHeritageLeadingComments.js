//// [test/cases/ttttDeclarationHeritageLeadingComments.ts] ////

//// [a.ts]
export interface S {}
export class E /* <T> */ implements S {}
export class F /* c */ {}
export class G<T> /* c */ implements S {}
export function foo /* c */ () {}
export const v /* c */ = 1;
export interface I /* c */ { a: 1 }
export type A /* c */ = 1;
//// [b.ts]
export class B {}
export class H /* c */ extends B {}
export class J /* c */ extends B /* d */ implements Object /* e */ {}
export class K /* c */
  // line
  extends B {}
//// [c.ts]
export interface J {}
export interface I /* c */ extends J {}
export interface I2<T> /* c */ extends J /* d */ {}


//// [a.js]
export class E /* <T> */ {
}
export class F /* c */ {
}
export class G {
}
export function foo /* c */() { }
export const v /* c */ = 1;
//// [b.js]
export class B {
}
export class H /* c *//* c */  extends B {
}
export class J /* c *//* c */  extends B /* d */ {
}
export class K /* c *//* c */ 
// line
 extends B {
}
//// [c.js]
export {};


//// [a.d.ts]
export interface S {
}
export declare class E/* <T> */  implements S {
}
export declare class F {
}
export declare class G<T>/* c */  implements S {
}
export declare function foo(): void;
export declare const v = 1;
export interface I {
    a: 1;
}
export type A = 1;
//// [b.d.ts]
export declare class B {
}
export declare class H/* c */  extends B {
}
export declare class J/* c */  extends B/* d */  implements Object {
}
export declare class K/* c */  extends B {
}
//// [c.d.ts]
export interface J {
}
export interface I/* c */  extends J {
}
export interface I2<T>/* c */  extends J {
}
