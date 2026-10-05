// @strict: true
// @lib: es2015
declare function q(a: string, v?: number[]): void;
declare function q(a: string, cb: (e: Error) => void): void;
q("x", e => {})
declare function cross<S, T>(a: Iterable<S>, b: Iterable<T>): [S, T][];
declare function cross<S, T, U>(a: Iterable<S>, b: Iterable<T>, reducer: (a: S, b: T) => U): U[];
const c = cross([2, 3], [5, 6], (a, b) => a + b)
