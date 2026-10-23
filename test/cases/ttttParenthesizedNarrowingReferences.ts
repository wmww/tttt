// @strict: true
// Parentheses are nodes to tsc: `(o!).k` isn't a non-null access (isNonNullAccess), so a nullable union isn't
// narrowed by its discriminant, and `(x.isB)()` has no narrowable argument (the binder's hasNarrowableArgument), so
// its `this is B` narrows nothing (bun's 32-C3, 25-C1, 21-C6)
function f(o: { k: "a"; a: 1 } | { k: "b" } | undefined) { if ((o!).k === "a") o!.a; if (o!.k === "a") o!.a; }
class A { isB(): this is B { return false; } }
class B extends A { b = 1; }
function g(x: A) { if ((x.isB)()) x.b; if (x.isB()) x.b; }
