// @strict: true
type S<T> = T extends unknown ? {[K in keyof T]: S<T[K]>} : T;
type P<T> = S<M<T>>;
type M<T> = { [K in keyof T]: T[K] extends object ? P<T[K]> : T[K] };
declare const full: P<{ o: { s: string } }>;
