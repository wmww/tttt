// @noEmit: true
// A type parameter list recovers the same way after `function f`, a method name, in a type and in a speculative arrow
// function (tsc's parseTypeParameters): a reserved word that is no element is TS1139 and skipped (`default` too,
// which the Parameters context, not entered yet, would take as a modifier)
function f<T, default>() {}
class C { m<T, default>() {} }
let o = { m<T, default>() {} };
type F = <T, default>() => void;
let a = <T, default>() => 1;
interface I { m<T, default>(): void; <U, default>(): void }
let a2 = <T, default>(x) => 1;
function g<T, while>() {}
function h<T, null>() {}
