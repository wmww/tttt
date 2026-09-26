// @strict: true
declare const sym: unique symbol;
type EmptyObject = {[sym]?: never};
type Keys<B> = { [K in keyof B]-?: B[K] extends typeof sym | EmptyObject ? never : K }[keyof B];
type Ex<O, KS> = { [K in keyof O as K extends KS ? never : K]: O[K] };
type P<T> = Ex<M<T>, Keys<M<T>>>;
type M<T> = { [K in keyof T]: T[K] extends string ? T[K] : T[K] extends object ? P<T[K]> : typeof sym };
type Example = { n: number; o: { s: string; }; };
declare const full: P<Example>;
