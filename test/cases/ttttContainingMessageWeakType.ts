// @noEmit: true
// A containing message chain (checkTypePredicate's TS2677, a member override's TS2416) stays on top of a weak type
// error (TS2559), which only replaces a head message (mutation testing seed 20261102, typeGuardFunctionErrors)
class A { a?: number }
class B { b!: number }
class Base { p!: A }
class D extends Base { p!: B }
function g(x: A): x is B { return true; }
