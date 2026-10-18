// An elaborated property's type is its literal checked again with the source's own type as its context (tsc's
// checkExpressionForMutableLocationWithContextualType): `value: true` stays `true` where the source says `boolean`, and
// an array literal is a tuple only where that context is one (inbox-zero's eval scenarios)
type C = { path: string; value: unknown };
type E =
  | { kind: "a"; changes: C[]; forbidden: string[] }
  | { kind: "b"; x: number };
type S = { id: string; expectation: E };
const e: E = { kind: "a", changes: [{ path: "p", value: true }] };
const xs: S[] = [{ id: "x", expectation: { kind: "a", changes: [{ path: "p", value: true }] } }];
const ys: S = { id: "x", expectation: { kind: "a", changes: [{ path: "p", value: true }] } };
const zs: E[] = [{ kind: "a", changes: [{ path: "p", value: true }] }];
type C2 = { path: string; value: string|number };
const ws: { e: { kind: "a"; changes: C2[]; forbidden: string[] } }[] = [{ e: { kind: "a", changes: [{ path: "p", value: true }] } }];
const vs: { e: { kind: "a"; changes: C[]; forbidden: string[] } }[] = [{ e: { kind: "a", changes: [{ path: "p", value: true }] } }];
