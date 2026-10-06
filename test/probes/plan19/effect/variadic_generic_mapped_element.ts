// A generic mapped type spread into a tuple is a variadic element (tsc's createNormalizedTupleType), related as a whole,
// not as a rest array of its element type: ArbAnn<any, readonly []> isn't ArbAnn<any, readonly [any]> (effect Schema.ts 9010;
// the elaboration's parameter names still differ: tsc expands the instantiated rest tuple, 'ctx' and 'arbitraries_0')
// @strict: true
interface Ctx { readonly depth: number }
interface Lazy<A> { (fc: string): A }
type ArbAnn<A, P extends ReadonlyArray<any> = readonly []> = (
  ...arbitraries: [...{ readonly [K in keyof P]: Lazy<P[K]> }, ctx: Ctx]
) => Lazy<A>
declare const x: ArbAnn<any, readonly []>
export const y: ArbAnn<any, readonly [any]> = x
declare const x2: ArbAnn<any, readonly []> | ((a: Lazy<any>) => Lazy<any>)
export const y2: ArbAnn<any, readonly [any]> | undefined = x2


type M<P extends ReadonlyArray<any>> = { readonly [K in keyof P]: Lazy<P[K]> }
export function f<P extends ReadonlyArray<any>, Q extends P>(
  a: (...args: [...M<P>, ctx: Ctx]) => 1, b: (...args: [...M<Q>, ctx: Ctx]) => 1,
  ta: [...M<P>, ctx: Ctx], tb: [...M<Q>, ctx: Ctx], ma: M<P>, mb: M<Q>
) {
  a = b; b = a;
  ta = tb; tb = ta;
  ma = mb; mb = ma;
}
