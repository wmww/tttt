// @strict: true
// A destructured parameter's object literal initializer is padded with the pattern's computed names of literal type
// too (tsc's padObjectLiteralType -> getPropertyNameFromBindingElement): `b` is an implicit any (TS7031), not a missing
// property of `{}` (TS2339), which an annotation keeps (seed 20261113, controlFlowParameter drop-as)
declare let s: "";
function f1(a: number | string | undefined = 1, { [(a = "")]: b } = {}) {}
function f2({ [""]: b } = {}) {}
function f3({ [s]: b } = {}) {}
function f4({ [s]: b }: {} = {}) {}
function f5({ a: b } = {}) {}
function f6({ ["a"]: b } = {}) {}
