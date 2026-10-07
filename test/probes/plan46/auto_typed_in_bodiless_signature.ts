// @noImplicitAny: true
let x = undefined;
interface I { m(a: typeof x): void }
type F = (a: typeof x) => void;
declare function d(a: typeof x): void;
function f(a: typeof x) {}
x = 1;
let x2 = undefined;
type T3 = typeof x2;
x2 = 1;
type T4 = typeof x2;
let y2 = [];
interface I2 { m(a: typeof y2): void }
y2.push(1);
let z2;
type F2 = (a: typeof z2) => void;
