// @strict: true
// An accessor's write type (its setter's annotation) is resolved under its own entry (tsc's getWriteTypeOfAccessors,
// TypeSystemPropertyNameWriteType), and a union or intersection property asks each member's write type when it is made
// (createUnionOrIntersectionProperty): a setter annotation that reaches its own property through one is circular, TS2502
// at the setter named after the property (bun 99-V4 getWriteTypeOfAccessors__01/02/03/06, getWriteTypeOfSymbol__01/02/04)
// @filename: indexed.ts
export {}
class A {
    get x(): number { return 1; }
    set x(v: (A | B)["x"]) {}
}
class B { x = ""; }
declare const a: A;
a.x = true;

// @filename: intersection.ts
export {}
class C {
    get x(): number { return 1; }
    set x(v: (C & D)["x"] | string) {}
}
interface D { x: number; }
declare const c: C;
c.x = true;

// @filename: own.ts
export {}
class E {
    get x() { return 1; }
    set x(v: E["x"]) {}
}
declare const e: E;
e.x = {};

// @filename: variable.ts
export {}
class F {
    get x(): number { return 1 }
    set x(v: typeof r | number) {}
}
class G { x = 1 }
declare const u: F | G;
const r = u.x;
const probe: never = r;

// @filename: query.ts
export {}
class H {
    get x(): number { return 1 }
    set x(v: (typeof w)["x"] | string) {}
}
class I { x = 1 }
declare const w: H | I;
w.x = "s";
