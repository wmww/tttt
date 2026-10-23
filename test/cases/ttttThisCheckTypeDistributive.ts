// @strict: true
// @noEmit: true

// A conditional type whose check type is `this` is distributive (tsc's isDistributive: the check type is a type
// parameter, the this-type one): its constraint goes through the this type's constraint, `1` for `this extends Foo`,
// and its branches mention its check type (bun's 09-C9b, 19-C4a, 19-C1f)
class Foo { a = 1; p!: this extends Foo ? 1 : 2; m() { const x: 1 = this.p; } }
interface I { k: "i"; p: this extends { k: "i" } ? 1 : 2 }
function fi<T extends I>(t: T) { const a: 1 = t.p; }
class K { p!: this extends string ? this : this; m() { this.p = this; } }
