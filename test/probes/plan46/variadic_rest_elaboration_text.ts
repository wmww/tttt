// Elaborations of a variadic rest: the rest compared as a whole at tsc's parameter count ('args', labels kept), and the
// keys of a constraint printed expanded (getIndexType with NoReducibleCheck has no keyof origin), keyof's operand
// not parenthesized when it's a type operator (`keyof readonly any[]`)
export function f<P extends ReadonlyArray<any>>(k: keyof P, s: string) {
  k = s;
}
declare let r: keyof (readonly any[]);
declare const s: string;
r = s;
interface Ctx { readonly depth: number }
interface Lazy<A> { (fc: string): A }
type M<P extends ReadonlyArray<any>> = { readonly [K in keyof P]: Lazy<P[K]> }
export function g<P extends ReadonlyArray<any>, Q extends P>(
  a: (...args: [...M<P>, ctx: Ctx]) => 1, b: (...args: [...M<Q>, ctx: Ctx]) => 1,
  ta: [...M<P>, ctx: Ctx], tb: [...M<Q>, ctx: Ctx], ma: M<P>, mb: M<Q>
) {
  a = b; b = a;
  ta = tb; tb = ta;
  ma = mb; mb = ma;
}
