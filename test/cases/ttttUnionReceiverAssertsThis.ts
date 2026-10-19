// A method asserting `this is T` mentions its class's or interface's `this` (tsc's ThisType): it is instantiated per
// receiver, so a union receiver's method is a union signature, which asserts nothing (no TS2775, no narrowing):
// babel's NodePath.ensureBlock
// @strict: true
interface A { type: "A"; body: number }
interface B { type: "B"; body: string }
type N = A | B
interface Over {
  ensure(this: P_Final): asserts this is P_Final<N & { body: number }>;
}
interface P<T extends N> extends Over { node: T }
type P_Final<T extends N = N> = T extends any ? P<T> : never
declare function h(p: P_Final): { fn: P_Final }
declare const p: P_Final
const { fn } = h(p)
fn.ensure()
let q = p
q.ensure()
const r = h(p).fn
r.ensure()
declare const s: P<A>
const t = s
t.ensure()
