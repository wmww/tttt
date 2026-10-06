// A union call whose parameters intersect into 100000+ combinations is TS2590 (tsc's checkCrossProductUnion; effect
// fiberRuntime 1397: reported at the current node, once per checker here)
type D = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9
type U = `${D}${D}${D}`
type A = { [K in U]: { a: K } }[U]
type B = { [K in U]: { b: K } }[U]
declare const f: ((x: A) => void) | ((x: B) => void)
declare const a: A
f(a)
// A property of three or more members, missing in the source, isn't intersected (tsc defers such property types)
interface P1 { readonly p?: A }
interface P2 { readonly p?: B }
interface P3 { readonly p?: A | B }
export const y: P1 & P2 & P3 = {}
