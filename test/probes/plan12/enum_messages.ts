// @strict: true
// A const holding a single-member enum member has the fresh type `E.x`; expressions print the regular one, which is `E` itself (errors too)
enum E { x }
const e2 = E.x;
let q: "z" = e2;
let r: "z" = E.x;
function takesE(p: E) { return p; }
let viaParam = takesE(E.x);
declare const s: "z" | "y";
if (e2 === s) {}
function g(p: E.x) { return p === s; }
