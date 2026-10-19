//// [test/cases/ttttLocalExpandoInstantiated.ts] ////

//// [ttttLocalExpandoInstantiated.ts]
// A function with expandos declared inside a generic function is an anonymous type instantiated with the outer type
// parameters (tsc's getTypeOfFuncClassEnumModule): its signature and its expandos' types
interface Attributes { key?: string }
declare class Component<P = {}, S = {}> { props: P; state: S }
export function c1<P extends Attributes>(x: P) {
  function Memoed(this: Component<P>, props: P) { return 1; }
  Memoed._forwarded = true;
  Memoed.original = x;
  return Memoed;
}
export const t1 = c1({ key: "a" });
const s: string | undefined = t1.original.key;
const n: number = t1.original;


//// [ttttLocalExpandoInstantiated.js]
export function c1(x) {
    function Memoed(props) { return 1; }
    Memoed._forwarded = true;
    Memoed.original = x;
    return Memoed;
}
export const t1 = c1({ key: "a" });
const s = t1.original.key;
const n = t1.original;


//// [ttttLocalExpandoInstantiated.d.ts]
interface Attributes {
    key?: string;
}
declare class Component<P = {}, S = {}> {
    props: P;
    state: S;
}
export declare function c1<P extends Attributes>(x: P): {
    (this: Component<P>, props: P): number;
    _forwarded: boolean;
    original: P;
};
export declare const t1: {
    (this: Component<{
        key: string;
    }, {}>, props: {
        key: string;
    }): number;
    _forwarded: boolean;
    original: {
        key: string;
    };
};
export {};
