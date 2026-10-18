# A function with expandos inside a generic function is never instantiated

Found 2026-10-18 by agent C (dtsdiff react-scan/packages/scan, `web/views/{inspector,toolbar}/index.d.ts`).

```ts
interface Attributes { key?: string }
declare class Component<P = {}, S = {}> { props: P; state: S }
export function c1<P extends Attributes>(x: P) {
  function Memoed(this: Component<P>, props: P) { return 1; }
  Memoed._forwarded = true;          // without this line it works
  return Memoed;
}
export const t1 = c1({});            // tsgo: { (this: Component<{}, {}>, props: {}): number; _forwarded: boolean; }
                                     // tttt: { (this: Component<P>, props: P): number; ... } (P leaks out)
```

tttt types a function with expando assignments as a TY_DECLARED namespace (`declared.namespace_symbol` the function,
see `is_nested_expando_function()` in ty_display.c), and `instantiate_inner()` returns TY_DECLARED types as they are.
tsc's is an anonymous object type (getTypeOfFuncClassEnumModule: ObjectFlags.Anonymous with the function's symbol),
which couldContainTypeVariables and getObjectTypeInstantiation instantiate with the declaration's outer type
parameters (P here): its signature and its expando members' types are instantiated.

This is a checker bug, not only a printing one: a call's result keeps the callee's type parameter (relations against
it, property types of the expandos). Fix direction: either make a nested (non file-level) expando function an anonymous
type with `merged_value` (members from the function and the expandos, `ty_merged_value_type()`'s anonymous branch
already reads overloads) so the anonymous instantiation machinery applies, or give TY_DECLARED namespaces of nested
functions a mapper. Check the printing paths that test `declared.kind == TY_DECLARED_NAMESPACE` for expandos
(`expando_displays_structurally`, `expando_cyclic_placeholder`). Validate with the probe above (tsgo's output in the
comment), react-scan in dtsdiff, and the usual oracles (sweep, flagflip, corpora: JS expandos go through the same
type).
