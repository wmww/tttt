// @strict: true
type P<T> = Pick<M<T>, keyof M<T>>;
type M<T> = { [K in keyof T]: T[K] extends object ? P<T[K]> : T[K] };
declare const x: keyof M<{ o: { s: string } }>;
declare const y: M<{ o: { s: string } }>;
declare const full: P<{ o: { s: string } }>;
const q: { o: { s: string } } = full;
