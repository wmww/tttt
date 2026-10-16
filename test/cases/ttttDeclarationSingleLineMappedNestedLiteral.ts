// A mapped type prints on one line in declaration emit (the node builder's EmitFlags.SingleLine), but only the mapped
// type node itself: a type literal inside it prints on several lines, at the mapped type's indentation (feathers'
// queryProperties printed `{ [K in keyof T]?: { [key: string]: JSONSchema; }; }`)
// @strict: true
// @declaration: true
export const reused = <T extends { [key: string]: number }, X extends { [K in keyof T]?: { [key: string]: string } }>(d: T, e?: X) =>
  d as any as { [K in keyof T]: { a: X[K]; b: 1 } };
declare function m<T>(t: T): { [K in keyof T]: { a: T[K]; b: { c: 1 } } };
export const instantiated = <U,>(u: U) => m(u);
export function nested<T>(t: T) {
  return { inner: { deep: null! as { [K in keyof T as `x${K & string}`]: { a: T[K] } } } };
}
