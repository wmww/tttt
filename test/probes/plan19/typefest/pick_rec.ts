// @strict: true
type P<T, O = {}> = Pick<M<T, O>, keyof M<T, O>>;
type M<T, O> = { [K in keyof T]: T[K] extends object ? P<T[K], O> : T[K] };
declare const full: P<{ o: { s: string } }>;
const q: { o: { s: string } } = full;
type P2<T> = Pick<M2<T>, keyof M2<T>>;
type M2<T> = { [K in keyof T]: T[K] extends object ? P2<T[K]> : T[K] };
declare const full2: P2<{ o: { s: string } }>;
const q2: { o: { s: string } } = full2;
