// @strict: true
// An `infer X` constrained by where it is written takes the constraint through tsc's getIntersectionType, even a
// single one: an unresolved name's error type there is the plain error type, which a conditional type stops at, so
// Rec<any> ends instead of recursing to the instantiation limit
declare class Box2<T extends Missing> { v: T }
type Un3<R> = R extends Box2<infer X> ? [X] : 0;
declare const a: Un3<any>;
const n: string = a;
type Rec<R> = R extends Box2<infer X> ? Rec<X> : 0;
declare const b: Rec<any>;
const m: string = b;
