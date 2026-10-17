// @strict: true
// A class whose base constructor is any has a string index signature of any on both sides (tsgo's
// anyBaseTypeIndexInfo): assignable to a Record, `keyof` includes string, a missing member is any. A base constructor
// that only constructs any (a mixin's type parameter) gives the instance side one, not the static side
declare const B: any;
class C extends B { x = 1; }
class D extends C { }
const r: Record<string, number> = new C();
const r2: Record<string, number> = new D();
const s: Record<string, number> = C;
const k: keyof D = "anything";
const a = new D().missing;
const t = C.missingStatic;

function mixin<T extends new (...args: any[]) => any>(base: T) {
    class M extends base { m = 1; }
    const i: Record<string, number> = new M();
    const st: Record<string, number> = M;
    return M;
}
