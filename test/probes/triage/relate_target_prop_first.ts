// The target property's type is asked for before the source's (tsc's isPropertySymbolTypeRelated), which decides
// which literal is made first: tsc prints x.type as "A" | "B" | "C"
type A = { type: "A" };
type B = { type: "B" };
type C = { type: "C" };
type MyUnion = A | B | C;
declare const b: B;
const a: A = b;
declare const x: MyUnion;
x.type;
