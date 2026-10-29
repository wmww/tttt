// @strict: true
// A parenthesized assignment target isn't a discriminant access (bun case 55-L08): `(x.kind) = v` in a condition
// doesn't narrow x, where `x.kind = v` and a parenthesized condition `((x.kind))` do
type U = { kind: 0; a: string } | { kind: 1; b: number };
function f1(x: U, v: 0 | 1) {
    if ((x.kind) = v) {
        x.b;
    } else {
        x.a;
    }
    if ((x["kind"]) ||= v) {
        x.b;
    }
    if (x.kind! = v) {
        x.b;
    }
}
function f2(x: U, v: 0 | 1) {
    if (x.kind = v) {
        x.b;
    }
    if (((x.kind))) {
        x.b;
    }
    if ((x).kind = v) {
        x.b;
    }
}
