// A variadic tuple element of a generic mapped type is checked through its constraint (tsc's checkTupleType with
// isArrayLikeType): over a type parameter constrained by an array it is array-like, over one that isn't, TS2574
interface Option<T> { value: T }
type Unzip<T> = { [K in keyof T]: T[K] extends Option<infer V> ? V : never };
declare function zip1<T, O extends Array<Option<any>>>(...others: O): Option<[T, ...Unzip<O>]>;
declare function zip2<T, O extends { a: Option<any> }>(...others: O): Option<[T, ...Unzip<O>]>;
