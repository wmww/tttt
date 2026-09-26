// @strict: true
type Keys<B> = { [K in keyof B]-?: B[K] extends number ? never : K }[keyof B];
type S<T> = T extends unknown ? {[K in keyof T]: S<T[K]>} : T;
type P<T> = S<Pick<M<T>, Keys<M<T>>>>;
type M<T> = { [K in keyof T]: T[K] extends object ? P<T[K]> : T[K] };
declare const full: P<{ n: number; o: { s: string } }>;
