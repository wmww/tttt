// Spreading an empty object still makes a spread literal (tsc's ObjectFlags.ContainsSpread from getSpreadType): its
// properties don't widen its siblings in a union (getPropertiesOfContext), nor make a union's missing property
// `undefined` (createUnionOrIntersectionProperty's WritePartial)
declare const c: boolean;
const o1 = c ? { a: 1 } : { b: 2, ...{} };
(c ? { a: 1 } : { ...{} }).a;
(c ? { a: 1 } : { b: 2 }).a;
