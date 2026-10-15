//// [test/cases/ttttDeclarationCommentsOnce.ts] ////

//// [ttttDeclarationCommentsOnce.ts]
// Comments around reused type arguments and type parameter constraints once; a property without a valueDeclaration
// (a mapped type's, a spread's fresh one) copies none
type P<A, B> = [A, B];
export type S<T extends string /** constraint */, U = number /** dflt */> = P<
  /**
   * first arg
   */
  T,
  /** second */
  U
>;
export type Q<T = /* a */ string> = P<T /** after T */, 1>;
interface Options {
  /** doc a */
  a?: boolean;
  /** doc b */
  b: number;
}
declare const o: Options;
export const p1 = (x: Partial<Options>) => ({ ...o, ...x });
export const p2 = (x: Partial<Options>) => x;
export const p3 = () => o as Readonly<Options>;
export const p4 = () => ({ ...o });


//// [ttttDeclarationCommentsOnce.js]
export const p1 = (x) => ({ ...o, ...x });
export const p2 = (x) => x;
export const p3 = () => o;
export const p4 = () => ({ ...o });


//// [ttttDeclarationCommentsOnce.d.ts]
type P<A, B> = [A, B];
export type S<T extends string /** constraint */, U = number /** dflt */> = P<
/**
 * first arg
 */
T, 
/** second */
U>;
export type Q<T = string> = P<T /** after T */, 1>;
interface Options {
    /** doc a */
    a?: boolean;
    /** doc b */
    b: number;
}
export declare const p1: (x: Partial<Options>) => {
    a?: boolean;
    b: number;
};
export declare const p2: (x: Partial<Options>) => Partial<Options>;
export declare const p3: () => Readonly<Options>;
export declare const p4: () => {
    /** doc a */
    a?: boolean;
    /** doc b */
    b: number;
};
export {};
