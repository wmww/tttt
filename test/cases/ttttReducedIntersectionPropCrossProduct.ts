// @strict: true
// @filename: a.ts
type D = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9";
interface C<P> { p: P }
export type El<P = any> = `${D}${D}${D}` | C<P>;
// @filename: b.ts
// tsc's getReducedType (inference from an intersection source) makes the intersection's property of a name exactly two
// members have and intersects their types at once: `(El<any> | undefined) & (El | undefined)` keeps El<any> and El, two
// unions of the same members (an imported alias referenced without arguments is its own instantiation), and their
// cross product is too complex (TS2590 at the call). chakra-ui's `as?: As` through React's element types
import { El } from "./a";
declare function f<T>(x: { a: T }): T;
declare const v: { as?: El<any> } & { as?: El } & { a: 1 };
f(v);
declare const w: [(El<any> | undefined) & (El | undefined)];
const n: number = w;
