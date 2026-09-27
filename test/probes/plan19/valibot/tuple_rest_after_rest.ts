// A spread of an array after a rest element merges into it (tsc's createNormalizedTupleType), and a lone rest element
// left is the array (valibot's LooseTupleSchema: [...InferTupleOutput<TItems>, ...unknown[]])
type M<T> = { -readonly [K in keyof T]: T[K] };
type G<X extends string[] | readonly string[]> = [...M<X>, ...unknown[]];
type T3 = G<string[] | readonly string[]>;
type H<X extends unknown[]> = [...X, ...number[]];
type T4 = H<string[]>;
type T5 = H<[string, ...boolean[]]>;
type O<X extends unknown[]> = [...X, boolean?];
type T6 = O<string[]>;
type RO<X extends unknown[]> = readonly [...X, ...number[]];
type T7 = RO<string[]>;
declare let a: unknown[];
declare let t3: T3;
t3 = a;
