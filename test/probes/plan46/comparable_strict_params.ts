// Signature parameters are strict (contravariant) under the comparable relation too, except a class constructor's,
// which stays bivariant also when a derived class inherits it (tsc's strictVariance goes by the cloned declaration)
// @strictFunctionTypes: true
interface A { a: string }
interface B extends A { b: string }
interface RA { p: A; x: 1 }
interface RB { p: B }
declare const f: (impl: A) => RA
export const r = f as (impl: B) => RB


declare class P { constructor(c: A[]); p: 1 }
declare class Q extends P {}
declare class P2 { constructor(c: B[]); p: 1 }
declare class Q2 extends P2 { q: 2 }
declare const q2: typeof Q2
export const r2 = q2 as typeof Q
export const s2: typeof Q = Q2
