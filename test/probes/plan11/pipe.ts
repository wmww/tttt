// @strict: true
// genericFunctionInference1-style: pipe with generic functions, and composition keeping type parameters
declare function pipe<A extends any[], B, C>(ab: (...args: A) => B, bc: (b: B) => C): (...args: A) => C;
declare function list<T>(a: T): T[];
declare function box<V>(x: V): { value: V };
const f00 = pipe(list, box);
const f01 = pipe((x: number) => x, list);
const r00 = f00(42);
declare function compose<A, B, C>(f: (b: B) => C, g: (a: A) => B): (a: A) => C;
const f02 = compose(list, box);
declare function wrap<T, R>(f: (x: T) => R): (x: T) => R;
const f03 = wrap(<U>(u: U) => [u]);
const r03 = f03<string>("a");
