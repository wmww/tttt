// @strict: true
// An index signature's parameters without a type annotation (or initializer) are implicitly any (tsc's checkParameter:
// TS7006, TS7019 for a rest), next to the grammar errors (seed 20261112's iterableArrayPattern2: `var [a, ...b] = x` in an
// open class body is a property `var` and an index signature)
interface I {
    [c, d]: number;
    [...f]: number;
    [g?]: number;
    [public h, k = 1]: number;
}
type T = { [m, n: string]: number };
class C {
    var [a, ...b] = x;
}
