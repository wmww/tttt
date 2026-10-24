// A generic mapped type over a lone generic mapped modifiers type that isn't homomorphic (Record's `[P in K]` with
// K = keyof T) has its index signatures, the template at their keys (tsc's resolveMappedTypeMembers over
// forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType): Object.entries infers from the string one (cella)
// @strict: true
// @lib: es2022
function f<T extends Record<string, string>>(o: Partial<Record<keyof T, string>>, r: Record<keyof T, string>, p: Partial<T>) {
  const e1 = Object.entries(o);
  const e2 = Object.entries(r);
  const e3 = Object.entries(p);
  type K1 = keyof Partial<Record<keyof T, string>>;
  const x: Partial<Record<keyof T, string>>[string] = null!;
}
