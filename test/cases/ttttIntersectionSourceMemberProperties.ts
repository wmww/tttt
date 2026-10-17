// @strict: true
// @noEmit: true

// A member of an intersection source has no inferable index signature, down into its properties too (tsc's
// IntersectionState.Source): `{ x: { a: number } }` alone doesn't make the intersection's `x` fit the index signature
type Target = { x: { [k: string]: number } };
declare let both: { x: { a: number } } & { x: { b: string } };
const t: Target = both;

declare let one: { x: { a: number } } & { y: string };
const u: Target = one;
