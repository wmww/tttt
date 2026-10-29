// @strict: true
// @noEmit: true

// Whether a rest argument's literal stays a literal goes by its contextual type instantiated with the call's
// returnMapper (tsc's checkExpressionWithContextualType -> instantiateContextualType): a `never` return context makes
// `T[0]` `never`, no literal context, and the spread tuple widens (mutation seed 20261124's genericRestParameters1)
declare function f11<T extends (string | number | boolean)[]>(...args: T): T;
declare function g<T extends string | number>(x: T): T;
const z10 = f11(42, "hello", true);
const z12: never = f11(42);
const z13: never = g(42);
const z14: [1] = f11(1);
const z15: string[] = f11("a");
const z16: [number, "b"] = f11(2, "b");
const z17: (1 | "c")[] = f11(1, "c");
