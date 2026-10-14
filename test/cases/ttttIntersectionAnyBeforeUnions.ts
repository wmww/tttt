// @strict: true
// tsc's getIntersectionType decides never, then any, before it intersects unions of primitives: an `any` member makes
// `boolean & any & (string | undefined)` any (a write through a union key of { a: boolean; b: any; c: string |
// undefined } takes any, vencord's settings.store[key] = value), but null or undefined next to an object, or two
// disjoint primitives, are never even with any
declare const anyv: any;
type I1 = boolean & any & (string | undefined);
let i1: I1 = anyv;
type I2 = any & (string | undefined) & boolean;
let i2: I2 = anyv;
type I3 = undefined & { a: 1 } & any;
let i3: I3 = anyv;
type I4 = (string | undefined) & { a: 1 } & any;
let i4: I4 = anyv;
type I5 = string & number & any;
let i5: I5 = anyv;
type I6 = boolean & string & any;
let i6: I6 = anyv;
type Store = { a: boolean; b: any; c: string | undefined };
declare const k: keyof Store;
declare const s: Store;
s[k] = anyv;
