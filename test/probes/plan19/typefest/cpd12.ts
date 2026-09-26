// @strict: true
type Keys<B> = { [K in keyof B]-?: B[K] extends number ? never : K }[keyof B];
type S<T> = T extends unknown ? {[K in keyof T]: S<T[K]>} : T;
type P<T> = S<Pick<T, Keys<T>>>;
declare const full: P<{ n: number; o: { s: string } }>;
