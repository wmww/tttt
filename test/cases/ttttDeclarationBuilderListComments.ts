// @declaration: true
// @strict: true
// The node builder's copies of a union or intersection keep the comments at their members' positions when the node is
// in the file being written (tsgo's visitor keeps the ranges, the printer's emitListItems writes the comments there),
// like the declaration transformer's: babel-parser's `/*?N.Placeholder<T>*/ MaybePlaceholder<T> | undefined` of a
// mixin's class expression. A copy of another file's node has none (the offsets are that file's).
// @filename: a.ts
type M<T> = { t: T };
export type N = { n: 1 };
class P {}
export default (S: typeof P) => class X extends S {
    a<T extends string>(e: T): /*?A<T>*/ M<T> | undefined { return undefined; }
    b<T extends string>(e: T): /*B<T>*/ M<T> { return null!; }
    c(e: /*P1*/ M<string> | /*P2*/ undefined, f: /*I1*/ N & /*I2*/ { x: 1 }): void {}
    d(e: M<string> /*T1*/ | undefined /*T2*/): /*R*/ Array</*E*/ string | /*U*/ number> { return []; }
    f: /*C*/ string | number = 1;
};
export class Y {
    a(e: /*P1*/ M<string> | /*P2*/ undefined): /*R*/ Array</*E*/ string | /*U*/ number> { return []; }
}
// @filename: b.ts
import { Q } from "./c";
export const q = new Q().a;
// @filename: c.ts
export type N = { n: 1 };
export class Q {
    a(e: /*P1*/ N | /*P2*/ undefined, f: /*S*/ string | /*U*/ undefined): /*R*/ N | undefined { return undefined; }
}
