// @target: es2020
class C {
    x = 1;
    self() { return this; }
    chain(): this { return this; }
}
class D extends C { y = 2; }
declare const d: D;
const r = d.chain().y;
const q = d.self();
declare const m: Map<string, number>;
const g = m.set("a", 1).get("a");
interface I { clone(): this; n: number }
declare const i: I;
const j = i.clone().n;
