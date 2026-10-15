// @declaration: true
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
