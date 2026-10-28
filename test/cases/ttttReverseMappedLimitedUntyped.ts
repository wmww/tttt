// @strict: true
// A reverse mapped type through `keyof T & "b"` has only the properties the limited constraint takes, and the others'
// types are never asked (tsc's resolveReverseMappedTypeMembers with getLimitedConstraint): a getter that reads the
// call's result isn't a cycle there (bun 99-V4 getTypeOfReverseMappedSymbol__05). Without the limit it is (__06).
declare function unbox<T>(x: { [K in keyof T & "b"]: [T[K]] }): T;
const src = {
  get a() { return out.b; },
  b: [1] as [number],
};
const out = unbox(src);
const probe: never = out;

declare function unboxAll<T>(x: { [K in keyof T]: [T[K]] }): T;
const src2 = {
  get a() { return out2.b; },
  b: [1] as [number],
};
const out2 = unboxAll(src2);
