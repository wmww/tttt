// tsc's returnMapper is a fixing mapper: a fresh literal inferred from the contextual return type widens, so the
// argument's contextual type is string and its literal widens too (a regular literal context keeps it)
declare function f<A extends B, B>(y: B, cb: (b: B) => void): A | B;
declare function g<B>(y: B, cb: (b: B) => void): B;
const k = "a";
k || f("b", b => { const t: "b" = b; });
f("b", b => { const t: "b" = b; });
k || g("b", b => { const t: "b" = b; });
g("b", b => { const t: "b" = b; });
const x1: "a" = f("b", b => { const t: "b" = b; });
declare function h<B>(y: B): B;
declare const k3: "a";
k3 || g("b", b => { const t: "b" = b; });
k || g("b", b => { const t: "b" = b; });
const r1 = k || h("b");
const r2 = k3 || h("b");
const r3: "a" | "b" = h("b");
declare const k5: "a" | undefined;
const r5 = k5 || h("b");
const r6 = k5 || g("b", b => { const t: "b" = b; });
