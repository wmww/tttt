// Narrowing a union by a predicate none of its members has anything in common with intersects each member with the
// candidate; the union of those reduces to never and has no properties (excalidraw duplicate.ts:342 under the sloppy
// flags: `isBoundToContainer(element)` after `hasBoundTextElement(element)` returned)
// @strict: false
type A = { type: "a"; x: number };
type B = { type: "b"; y: number };
type C = { type: "c"; z: number };
declare function isC(v: unknown): v is C;
declare const ab: A | B;
if (isC(ab)) {
  ab.zz;
}
