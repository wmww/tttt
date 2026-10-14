// @noImplicitAny: true
// A getter whose `{` the parser lost has a missing, empty body (tsc's parseFunctionBlock): void, no TS7033. One
// where a `;` could go has none: implicitly any. A method's missing body is any (getReturnTypeOfSignature's NodeIsMissing)
function f() => 4;
var o = { get e, get g(), m() x, n, get h
};
class C { m() x; get k() y }
