// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type In<t> = t extends string ? string : never

// arktype's `attest(actual).satisfies(def)`: an error in `actual` makes `expected` the error type
type overlaps<l, r> = l & r extends never ? false : true
type Assertions<expected> = { satisfies: <def>(def: overlaps<expected, In<def>> extends true ? unknown : "no overlap") => void }
declare function attest<expected>(actual: expected): Assertions<expected>
declare const n: never
attest(n.foo).satisfies(1)

// the same with a plain `any`
interface C<T> {
	m<U>(x: T & In<U>): void
	p1<U>(y: T & In<U>, x: U): void
	p2<U>(y: T & In<U>, x: U): void
}
declare const c: C<any>
c.m(1)
c.m<number>(1)
c.p1(2, 1)
c.p1<number>(2, 1)
// nothing has asked for the type of `y` of `C<any>.p2` before it is instantiated: 'never' in both
c.p2<number>(2, 1)
class K<T> { m<U>(x: T & In<U>): void {} }
new K<any>().m(1)
declare function f<T>(): <U>(x: T & In<U>) => void
f<any>()(1)
const sentinel: number = "";
