// @strict: true
// @lib: es2015
declare function cross<T extends unknown[]>(...iterables: { [K in keyof T]: Iterable<T[K]> }): T[];
declare function cross<T extends unknown[], U>(...args: [...iterables: { [K in keyof T]: Iterable<T[K]> }, reducer: (...values: T) => U]): U[];
const r: string[] = cross([2, 3], [5, 6], (a, b) => (a + b) + "px");
