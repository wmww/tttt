// @noImplicitAny: true
// `x.push` and `x.unshift` passed to a call read x as an evolving array (tsc's isEvolvingArrayOperationTarget: the
// access's parent is a call expression), without finalizing it nor adding to it
declare function f(g: (...items: any[]) => number): void;
function a() {
    let x = [];
    f(x.push);
    x.push(1);
    return x;
}
function b() {
    let y = [];
    y.push("s");
    f(y.unshift);
    return y;
}
function c() {
    let z = [];
    (function (g: unknown) {})(z.push);
    z.push(true);
    return z;
}
