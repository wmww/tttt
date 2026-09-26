type Pk<K extends string> = {[P in K]: 1};
type P<T> = Pk<keyof M<T> & string>;
type M<T> = { [K in keyof T]: P<T[K]> };
declare const full: P<{ o: 1 }>;
const q: { o: unknown } = full;
