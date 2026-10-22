// @strict: true
// A variadic element instantiated to something that isn't array-like is a rest element of the error type (tsc's
// createNormalizedTupleType: "treat everything else as an array type"): `[1, ...any[]]`, not `[1, ...undefined]`
type T1<A extends any[], P extends any[][]> = [...A, ...P[0]];
type R1 = T1<[1], []>;
type G<A extends any[], P extends any[][], N> = [...A, ...P[0]][N & number];
export const q: { r1: R1, g: G<[1], [], 5> } = null!;
export const z: number = q;
