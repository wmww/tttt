// @strict: true
// Assignments where tsc's markNodeAssignments doesn't walk (an enum, a type node: a heritage clause's expression, an
// instantiation expression, a type literal's computed key) don't end the narrowing a closure sees; a call's callee with
// type arguments isn't a type node (bun's 21-C23, 32-C14a-d)
function f1(x: string | number) { if (typeof x === "string") () => x.length; enum E { A = (x = 1) } }
function f2(x: string | number) { if (typeof x === "string") { () => x.length; class D extends (x = 1, Object) {} } }
declare function gf<T>(): T;
function f3(x: string | number) { if (typeof x === "string") { () => x.length; (x = 1, gf)<number>; } }
function f5(x: string | number) { if (typeof x === "string") { () => x.length; (x = 1, gf)<number>(); } }
function f6(x: string | number) { if (typeof x === "string") { () => x.length; gf<typeof x>(); } }
