// @noEmit: true
// A call or `new` missing its `)` is incomplete (tsc's hasCorrectArity: the argument list ends where the call does):
// too few arguments are fine, so overload resolution goes on to the argument types (mutation seed 20261103,
// recursiveTypeRelations: `reduce<C>(cb` cut off at a `}` is TS2345 on the callback, not TS2554)
declare function f(a: number, b: number): void;
declare class C { constructor(a: number, b: number) }
declare function g(a: number, b: number): void;
declare function g(a: string): void;
function h() {
    f(1
}
function i() {
    new C(1
}
function j() {
    g(true
}
