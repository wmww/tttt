// @strict: true
// A `declare` field without a type or initializer takes the base class property's type when there is no constructor
// (a static one: no static blocks), tsc's getTypeOfPropertyInBaseClass, instead of being TS7008 any (type_core.md)
class B { x: string = ""; y?: number; static s: boolean = true; z: number = 1 }
class D extends B { declare x; declare y; declare z?; declare w; }
const s: number = new D().x;
const t: string = new D().y;
const u: string = new D().z;
class E extends B { declare x; constructor() { super(); } }
const v: number = new E().x;
class F extends B { declare static s; declare static x; }
const w: number = F.s;
const k: number = F.x;
class G extends B { declare static x; static { } }
class H<T> { a!: T }
class I extends H<string[]> { declare a; }
const l: number = new I().a;
class J extends B { declare z; constructor(c: boolean) { super(); if (c) this.z = 2; } }
const m: string = new J(true).z;
class K extends B { declare static x; static { K.x = "a"; } }
const n: number = K.x;
