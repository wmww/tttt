// @noLib: true

// Without lib there is no global Array: arrays are `{}` (tsc's emptyGenericType fallback), but tuples are still tuples,
// and spreading, destructuring or iterating one must not assume its array base type is a reference to Array (it is
// the empty object type, whose number index tsc reads as any).
declare const u: [number, string];
function h(a: number, ...b: string[]) {}
h(...u);
declare function k<T extends any[]>(...a: T): T;
const kr = k(...u);
const [p, ...qs] = u;
const spread = [...u, true];
for (const z of u) {}
function m(...a: [number, string]) { return a; }
m(1, "x");
m(...u);
const len = u.length;
declare const tu: [number] | [string, boolean];
tu.map;
const tuSpread = [...tu];
declare const ro: readonly [number, string];
const [r0, ...rs] = ro;
for (const z of ro) {}
h(...ro);
tu.forEach(() => {});
