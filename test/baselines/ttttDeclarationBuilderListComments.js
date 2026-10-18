//// [test/cases/ttttDeclarationBuilderListComments.ts] ////

//// [a.ts]
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
//// [b.ts]
import { Q } from "./c";
export const q = new Q().a;
//// [c.ts]
export type N = { n: 1 };
export class Q {
    a(e: /*P1*/ N | /*P2*/ undefined, f: /*S*/ string | /*U*/ undefined): /*R*/ N | undefined { return undefined; }
}


//// [a.js]
class P {
}
export default (S) => class X extends S {
    a(e) { return undefined; }
    b(e) { return null; }
    c(e, f) { }
    d(e /*T2*/) { return []; }
    f = /*C*/ 1;
};
export class Y {
    a(e) { return []; }
}
//// [c.js]
export class Q {
    a(e, f) { return undefined; }
}
//// [b.js]
import { Q } from "./c";
export const q = new Q().a;


//// [a.d.ts]
type M<T> = {
    t: T;
};
export type N = {
    n: 1;
};
declare class P {
}
export default _default;
declare function _default(S: typeof P): {
    new (): {
        a<T extends string>(e: T): /*?A<T>*/ M<T> | undefined;
        b<T extends string>(e: T): M<T>;
        c(e: /*P1*/ M<string> | /*P2*/ undefined, f: /*I1*/ N & /*I2*/ {
            x: 1;
        }): void;
        d(e: M<string> | undefined): Array</*E*/ /*E*/ string | /*U*/ number>;
        f: /*C*/ string | number;
    };
};
export declare class Y {
    a(e: /*P1*/ M<string> | /*P2*/ undefined): Array</*E*/ /*E*/ string | /*U*/ number>;
}
//// [c.d.ts]
export type N = {
    n: 1;
};
export declare class Q {
    a(e: /*P1*/ N | /*P2*/ undefined, f: /*S*/ string | /*U*/ undefined): /*R*/ N | undefined;
}
//// [b.d.ts]
export declare const q: (e: import("./c").N | undefined, f: string | undefined) => import("./c").N | undefined;
