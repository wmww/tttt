declare let a: any;
type N = { k: "a" } & { k: "b" };
let x: N = a;
declare function f(n: N): void;
f(a);
type IsAssignable<S, T> = [S] extends [T] ? true : false;
type R = IsAssignable<any, N>;
let r: R;
function g(n: N | string) { if (typeof n !== "string") { n; } }
