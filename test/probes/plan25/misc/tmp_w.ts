// @strict: true
interface O3<T> { src: T; cb: (v: T) => unknown; }
declare function f3<T>(o: O3<T>): T;
const a3 = f3({ src: { id: "" }, cb: (v) => {} });
declare function f4<T>(o: { src: T; cb: (v: T) => unknown }): T;
const a4 = f4({ src: { id: "" }, cb: (v) => {} });
declare function f5<T>(o: { src: T; cb: (v: T) => void }): T;
const a5 = f5({ src: { id: "" }, cb: (v) => {} });
