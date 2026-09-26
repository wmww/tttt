// @strict: true
type Keys<B> = { [K in keyof B]-?: B[K] extends number ? never : K }[keyof B];
type S<T> = T extends unknown ? {[K in keyof T]: S<T[K]>} : T;
type P<T, O = {}> = S<Pick<M<T, O>, Keys<M<T, O>>>>;
type M<T, O> = { [K in keyof T]: T[K] extends object ? P<T[K], O> : T[K] };
declare const full: P<{ n: number; o: { s: string } }>;
const q: { o: { s: string } } = full;
