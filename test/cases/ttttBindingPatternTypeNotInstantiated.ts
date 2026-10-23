// @strict: true
// A binding pattern's implied type has no symbol, so instantiation leaves it as it is (tsc's
// couldContainTypeVariables of an anonymous type asks its symbol): `{ a?: T }` stays that in `f<string>`
function f<T>({ a = null! as T }) {}
f<string>({ a: "x" });
function g<T>(x: T) { return ({ a = x }) => a; }
g(1)({ a: 1 });
function h<T>([a = null! as T]) {}
h<string>(["x"]);
