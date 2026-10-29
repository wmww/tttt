// @strict: true
// A generic interface has a `this` type even when only an accessor mentions it (31-P3's follow-up): tsc's
// getDeclaredTypeOfClassOrInterface asks isThislessInterface only of an interface without type parameters
interface G<T> { get x(): this; v: T }
declare const g: G<number>;
const r = g.x;
interface H { get x(): this }
declare const h: H;
const s = h.x;
interface K<T> extends G<T> { w: T }
declare const k: K<string>;
const t = k.x;
