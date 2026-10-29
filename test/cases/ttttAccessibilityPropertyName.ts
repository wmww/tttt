// TS2341/TS2445 name the property as symbolToString does, as written: `"a-b"` and `"1n"` quoted. A destructuring
// assignment's bigint key `1n` reads the property "1n" (tsgo's getLiteralTypeFromPropertyName), TS1539 aside
let s: any;
class Q { private "a-b" = 1; private "1n" = 2; protected "p q" = 3; }
({ "a-b": s } = new Q());
new Q()["a-b"];
const { "a-b": z } = new Q();
({ 1n: s } = new Q());
const { 1n: y } = new Q();
const { "p q": w } = new Q();
