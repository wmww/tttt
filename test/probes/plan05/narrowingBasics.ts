// @strict: true
interface A { kind: "a"; a: number }
interface B { kind: "b"; b: string }
declare function isString(x: unknown): x is string;
declare function assert(x: unknown): asserts x;
declare function assertIsA(x: A | B): asserts x is A;
declare function fail(): never;
declare function use(x: string): void;
class C { c = 1 }
class D { d = "" }

function truthiness(x: string | undefined, y: string | null) {
    use(x); // error
    if (x) use(x);
    if (!x) return;
    use(x);
    use(y && y.length > 0 ? y : "");
    const z = x || y;
}
function typeofs(x: string | number | boolean | (() => void)) {
    if (typeof x === "string") use(x);
    else if (typeof x === "function") x();
    else { const n: number | boolean = x; }
    switch (typeof x) { case "number": return x.toFixed(); case "string": return x; }
    x;
}
function equality(x: string | undefined, y: "p" | "q" | 1) {
    if (x !== undefined) use(x);
    if (x != null) use(x);
    if (x === undefined) return;
    use(x);
    if (y === "p") { y; } else { y; }
    if (y === 1 || y === "q") return;
    y;
}
function discriminants(v: A | B, o?: { v: A | B }) {
    if (v.kind === "a") v.a; else v.b;
    switch (v.kind) { case "a": v.a; break; default: v.b; }
    if (o?.v.kind === "a") o.v.a;
    if ("a" in v) v.a; else v.b;
}
function instances(x: C | D, u: unknown) {
    if (x instanceof C) x.c; else x.d;
    if (isString(u)) use(u);
    assert(typeof u === "number");
    u.toFixed();
}
function asserts2(v: A | B, m: string | undefined) {
    assertIsA(v);
    v.a;
    if (!m) fail();
    use(m);
}
function assignments() {
    let x: string | number = "s";
    use(x);
    x = 1;
    x.toFixed();
    let y: string | undefined;
    use(y); // error: used before assigned
    for (let i = 0; i < 3; i++) { x = cond() ? "s" : i; }
    x;
}
declare function cond(): boolean;
function loops(n: A | B | undefined) {
    let cur: { next?: typeof cur } | undefined = {};
    while (cur) { cur = cur.next; }
    cur;
    let v: string | number = 0;
    while (cond()) { v = typeof v === "number" ? "s" : 0; if (cond()) continue; v; }
    v;
}
function closures(x: string | undefined, p: { q?: string }) {
    const c = x;
    if (c && p.q) {
        const f = () => use(c);
        const g = () => use(p.q); // error
        use(p.q);
    }
    if (x) { [1].forEach(() => use(x)); }
}
function reach(x: number): number {
    if (x) { return 1; } else { throw x; }
    x; // unreachable
}
function noReturn(x: number): number { // error 2366
    if (x) return 1;
}
function neverInferred() { return fail(); }
class P { q?: string; m() { if (this.q) use(this.q); } }
