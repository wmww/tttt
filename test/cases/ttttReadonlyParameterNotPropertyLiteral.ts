// @strict: true
// A `readonly` parameter outside a constructor isn't a parameter property (TS2369): like a readonly property, its
// initializer keeps its literal type (tsc's isDeclarationReadonly)
function f(readonly h = 1) { const t: 1 = h; }
function g(h = 1) { const t: 1 = h; }
const a = (readonly s = "x") => s;
class C {
    constructor(readonly h = 1) { const t: 1 = h; }
    m(readonly k = true) { return k; }
}
declare class D {
    constructor(readonly h = 1);
}
