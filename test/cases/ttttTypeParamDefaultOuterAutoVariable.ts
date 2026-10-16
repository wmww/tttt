// A function's type parameters are in its flow container (tsc's binder): `<T = typeof x>` of an outer auto-typed
// `let x` reads it from the function's start, implicitly any (TS7034, TS7005), like a parameter's type does. A type
// alias's are in the flow around it (mutation of contextualTypesNegatedTypeLikeConstraintInGenericMappedType1)
// @strict: true
let D = undefined;
type A<T extends "a" | "b" = typeof D> = T;
interface I { <T extends "a" | "b" = typeof D>(): null }
let F = undefined;
function f<T = typeof F>() {}
let H = undefined;
declare function h<T = typeof H>(): void;
let K = undefined;
const k = <T = typeof K>() => {};
let M = undefined;
class C { m<T extends typeof M>() {} }
