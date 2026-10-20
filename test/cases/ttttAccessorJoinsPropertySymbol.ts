// A property symbol an accessor joins (merged interfaces and classes) is typed by the accessors, without the
// property's optionality (tsc's getTypeOfSymbol takes getTypeOfAccessors first): TS2717 and TS2687 against them, and
// TS2564 by the accessor's type
interface Foo2 {
    x?: string
}
class Foo2 {
    accessor x: string = "abc"
}
class Foo4 {
    accessor z: string = "abc"
}
interface Foo4 {
    z?: string
}
interface Foo5 {
    w: number
}
class Foo5 {
    accessor w: string = "abc"
}
interface A {
    x: number
}
interface A {
    get x(): string;
}
declare const a: A;
a.x = "s";
const n: number = a.x;
interface C {
    get y(): string;
    set y(v: string);
}
interface C {
    y?: number;
}
declare const c: C;
const cy: string = c.y;
class D { z?: number }
interface D { get z(): string }
declare const d: D;
const dz: string = d.z;
class E { accessor w: string = "" }
interface E { w?: number }
declare const e: E;
const ew: string = e.w;
const e2: E = {} as { w: string };
