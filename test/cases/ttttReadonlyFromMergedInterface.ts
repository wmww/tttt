// @target: es2022
// A readonly property assigned through `this` in a constructor must be declared in that class (tsc's
// isAssignmentToReadonlyEntity: the value declaration's parent), not by an interface merged with it (missing_checks.md)
interface C { readonly x: number }
class C { constructor() { this.x = 1; } }
class D { readonly y: number; constructor(readonly z: number) { this.y = 1; this.z = 2; } }
interface D { readonly w: string }
class E { readonly v: number; constructor() { this.v = 1; } }
interface E { readonly v: number }
interface F { readonly u: number }
class F { readonly u: number; constructor() { this.u = 1; } }
