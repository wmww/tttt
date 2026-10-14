// @target: es2015
// A getter with a body in a type (TS1183) is bound, walked and checked, and its unannotated type is what it returns
type A = { get foo() { return 0 } };
declare const a: A;
const n: string = a.foo;
interface X { get foo() { return "s" }; set foo(v) { } }
declare const x: X;
x.foo = 1;
type C = { set baz(v: number); get baz() { return 1 } };
interface D { get q() { const r = this; return r; } }
