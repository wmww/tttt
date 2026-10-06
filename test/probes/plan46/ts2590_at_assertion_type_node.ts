// TS2590 from an `as` type is reported at the type node (tsc checks it under checkSourceElement(type)); the later
// lines are tsc reporting again each time it rebuilds the intersection, which tttt caches (known divergence)
type D = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9
type U = `${D}${D}${D}`
type A = { [K in U]: { a: K } }[U]
type B = { [K in U]: { b: K } }[U]
declare const a: A
declare const b: B
export const x = Math.random() ? a as A & B : b
// A property only the target's members have isn't intersected: no cross product of their types
interface P1 { readonly p?: A }
interface P2 { readonly p?: B }
export const y: P1 & P2 = {}
declare const q: { q: 1 }
export const z: P1 & P2 & { q: 1 } = q
