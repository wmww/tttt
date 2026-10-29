// @strict: false
// @target: es2020
// A `new` of something with call signatures only is TS2350 for a non-void signature also when the arguments don't fit
// (mutation seed 20261123's mut1522: tsc's resolveCall returns the overload failure candidate)
new BigInt(123, 1);
declare function f(a: number): number;
new f("x");
declare function g(this: void, a: number): void;
new g("y", 2);
