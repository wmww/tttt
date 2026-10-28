// @strict: true
// A base constructor that constructs a union, a primitive, unknown, a type parameter or a generic mapped type: TS2509 (tsc's isValidBaseType of the reduced base), not only an intersection reduced to never
interface A { a: 1 }
interface B { b: 1 }
declare const base1: new () => A | B
class D1 extends base1 { }
declare const base2: new () => A & (A | B)
class D2 extends base2 { }
type Wr<T> = { -readonly [K in keyof T]: T[K] }
declare const base3: new () => Wr<A> | A
class D3 extends base3 { }
declare const s: new () => string
class E1 extends s {}
declare const n: new () => null
class E2 extends n {}
function f<T, U extends object, V extends string>(a: new () => T, b: new () => U, c: new () => V, d: new () => Readonly<U>, e: new () => Partial<{x: 1}>) {
    class F1 extends a {}
    class F2 extends b {}
    class F3 extends c {}
    class F4 extends d {}
    class F5 extends e {}
    const g = class extends c {}
}
declare const u: new () => unknown
class E3 extends u {}
declare const nv: new () => never
class E4 extends nv {}
declare const nvi: new () => { a: 1 } & { a: 2 }
class E5 extends nvi {}
declare const ov: { new (): string; new <T>(x: T): { a: T } }
class E6 extends ov {}
