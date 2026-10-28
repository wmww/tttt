// A union property where a member has no property but a template literal index signature for the name: the
// harness prints the union property's symbol (tsc's createUnionOrIntersectionProperty, getApplicableIndexInfoForName)
declare const v: { b: 1 } | { readonly [k: `b${string}`]: 1 };
const x = v.b;
declare const w: { b: 1 } | { [k: string]: 2 };
const y = w.b;
declare const u: { b: 1 } | { [k: `b${string}`]: 1 };
const z = u.b;
