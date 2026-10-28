// @strict: true
// Two write types tsc doesn't take from a setter (bun 99-V4 getWriteTypeOfSymbol__03/05): class accessors merged with
// an interface property of their name are a SymbolFlags.Property, written as read (getWriteTypeOfSymbol), whichever
// comes first; and a union member that only has an index signature for the name adds its value type to the union
// property's read types, not to its write types (createUnionOrIntersectionProperty)
// @filename: merged.ts
export {}
class C {
  get x(): string { return "" }
  set x(v: string | number) {}
}
interface C { x: string }
declare const c: C;
c.x = 1;
interface D { y: string }
class D {
  get y(): string { return "" }
  set y(v: string | number) {}
}
declare const d: D;
d.y = 1;

// @filename: indexed.ts
export {}
class A {
  get x(): string { return "" }
  set x(v: string | number) {}
}
declare const u: A | { [k: string]: boolean };
const read: never = u.x;
u.x = 1;
u.x = true;
