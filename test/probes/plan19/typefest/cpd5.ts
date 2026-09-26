// @strict: true
declare const sym: unique symbol;
type EmptyObject = {[sym]?: never};
type Keys<B> = { [K in keyof B]-?: B[K] extends typeof sym | EmptyObject ? never : K }[keyof B];
type P<T> = Pick<M<T>, Keys<M<T>>>;
type M<T> = { [K in keyof T]: T[K] extends string ? T[K] : T[K] extends object ? P<T[K]> : typeof sym };
type Example = { n: number; o: { s: string; }; };
declare const full: P<Example>;
