// @strict: true
// An instantiation expression keeps a signature whose constraint its type arguments violate, uninstantiated (tsc's
// getInstantiatedSignatures); hacks.md `apply_type_arguments_part`: tttt dropped it, so a call it fits was an error
declare function f<T extends string>(x: T): T;
declare function f<T>(x: T[]): T;
const g = f<number>;
g([1]);
g("s");
const h = f<"a">;
