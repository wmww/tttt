type Mutable<T extends object> = { -readonly [K in keyof T]: T[K]; };
interface N { readonly kind: number; readonly postfix: boolean }
declare function mk(): N;
function f<T extends N>(p: boolean): T {
    const node = mk() as Mutable<T>;
    node.postfix = p;
    return node;
}
type Partial2<T> = { [K in keyof T]?: T[K] };
function g<T>(x: Partial2<T>): T {
    return x;
}
