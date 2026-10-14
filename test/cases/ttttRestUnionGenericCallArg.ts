// @strict: true
// @noEmit: true
// A generic call returning a function, left out of a generic call's inference (tsc's SkipGenericFunctions), is typed
// against its element of the instantiated rest parameter in the applicability check (getSpreadArgumentType): a union
// of tuples gives `produce(v => v)` the union of their elements there (solid's `setStore("a", ..., "h", produce(v =>
// ({ i: v.i })))` under SetStoreFunction's fallback overload)
declare function produce<T>(fn: (state: T) => void): (state: T) => T;
type S<T> = T | ((prev: T, tr: string[]) => T);
declare function g4<K>(k: K, ...rest: ["i", S<number>] | ["i", "a", S<string>]): void;
g4(1, "i", produce(v => v));
declare function g5<K>(k: K, ...rest: ["i", S<number>]): void;
g5(1, "i", produce(v => v));
declare function g6<K>(k: K, a: "i", b: S<number>): void;
g6(1, "i", produce(v => v));
