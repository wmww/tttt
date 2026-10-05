// TS2448: a variable used in the field initializers and static blocks of the class expression initializing it (not in
// its methods or functions)
// @target: es2022
const C = class {
    y = C;
    z = new C().y;
    m() { return C; }
    static s = C;
    static { C; }
    static { const f = () => C; }
};
const D = class {
    static s = D.t;
    static t = 1;
};
let E = { k: class { a = E; } };
const F = (() => class { a = F; })();
const G = function () { return class { a = G; }; };
