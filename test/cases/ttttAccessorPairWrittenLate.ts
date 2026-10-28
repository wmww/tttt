// @strict: true
// @target: es2022
// A written accessor and a late-bound one of the same name are one symbol (tsc's late-bound symbol merges into the
// early one): read from the getter, written through the setter
declare const k: "x";
declare const s: unique symbol;
class C {
    get x(): number { return 1; }
    set [k](v: string) {}
}
declare const c: C;
const r = c.x;
c.x = 1;
c.x = "a";
class D {
    get [k](): number { return 1; }
    set x(v: string) {}
}
declare const d: D;
const t = d.x;
d.x = "a";
d.x = 1;
class E {
    static get x() { return 1; }
    static set [k](v) {}
    get y() { return ""; }
    set y(v) {}
    get [s]() { return true; }
}
E.x = 2;
E.x = "a";
new E().y = "b";
const u = new E()[s];
class F {
    get x() { return 1; }
}
class G extends F {
    get [k]() { return 2; }
    set x(v: number) {}
}
new G().x = 3;
