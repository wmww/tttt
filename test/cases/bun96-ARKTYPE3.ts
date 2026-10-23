// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type In<t> = t extends string ? string : never
type Any<t> = t extends string ? string : any
interface C<T> {
	r<U>(x: U): T & In<U>
	u<U>(x: U): T | Any<U>
	g<U>(x: unknown, y: U): x is T & In<U>
}
declare const c: C<any>
c.r(1).foo
c.r<number>(1).foo
declare const d: C<unknown>
const e1: number = d.u(1)
const e2: number = d.u<number>(1)
declare const x: string | number
if (c.g(x, 1)) { x.foo }
// the type arguments that the port does take in two steps
c.r<never>(1 as never).foo
const e3: number = d.u<any>(1)
const sentinel: number = "";
