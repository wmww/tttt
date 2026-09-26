// @strict: true
type S<T> = T extends unknown ? {[K in keyof T]: S<T[K]>} : T;
type P<T> = S<Pick<T, keyof T>>;
type P2<T> = S<T>;
declare const a: P<{ o: string }>;
declare const b: P2<{ o: string }>;
