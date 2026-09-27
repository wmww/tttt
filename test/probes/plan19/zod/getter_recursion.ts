// @strict: true
interface Box<S> { shape: S; tag: "box" }
declare function arr<X>(x: X): { el: X };
declare function obj1<T extends Record<string, any>>(s: T): Box<T>;
declare function obj2<T extends Record<string, any>>(s: T): Box<{ -readonly [P in keyof T]: T[P] }>;
declare function obj3<T>(s: T): Box<T>;
const C1 = obj1({ name: 1, get sub() { return arr(C1); } });
const C2 = obj2({ name: 1, get sub() { return arr(C2); } });
const C3 = obj3({ name: 1, get sub() { return arr(C3); } });
const s1 = C1.shape.sub;
const s2 = C2.shape.sub;
const s3 = C3.shape.sub;
