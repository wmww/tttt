// @strict: true
// A mapped type over `never` with an `as` clause: tsc's forEachType hands never itself to the clause, which may still
// name a property (`{ [K in never as "a"]: 1 }` has a); without one, or with a clause that maps never to never, nothing
// (bun's 14-C10)
type M1 = { [K in never as "a"]: 1 };
declare let m1: M1;
const m1b: string = m1.a;
type M2 = { [K in never]: 1 };
declare let m2: M2;
m2.a;
type M3<T extends string> = { [K in T as `x${K}`]: K };
declare let m3: M3<never>;
const m3b: string = m3.x;
type M4 = { [K in never as `p${K}` | "q"]: K };
declare let m4: M4;
const z5: never = m4.q;
