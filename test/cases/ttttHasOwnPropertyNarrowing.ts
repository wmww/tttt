// @strict: true
// `x.hasOwnProperty("a")` removes `x.a`'s missing only for a property access called with a string literal, neither
// parenthesized, and only when no type predicate of the call decides first (tsc's narrowTypeByCallExpression; bun's
// 20-C5c, 20-P1, 20-P6, 32-C7)
function c0(x: { a?: number }) { if (x.hasOwnProperty("a")) { x.a.toFixed(); } }
function c1(x: { a?: number }) { if ((x.hasOwnProperty)("a")) { x.a.toFixed(); } }
function c2(x: { a?: number }) { if (x.hasOwnProperty(("a"))) { x.a.toFixed(); } }
interface H { a?: number; hasOwnProperty(k: string): this is H; }
function c3(x: H) { if (x.hasOwnProperty("a")) { x.a.toFixed(); } }
