// @strict: true
// The constraint of `C["groups"][number]` with `groups?: X[]` is nothing: undefined in `X[] | undefined` has no index
// signature (tsc's getIndexedAccessTypeOrUndefined without an access node), so G isn't known to be an array (TS2344)
type Config = { groups?: any[] };
type Need<G extends unknown[]> = G;
type A<C extends Config, G extends C["groups"][number]> = Need<G>;
type Config2 = { groups: any[] };
type B<C extends Config2, G extends C["groups"][number]> = Need<G>;
type Config3 = { groups?: string[][] };
type D<C extends Config3, G extends C["groups"][number]> = Need<G>;
type E<C extends Config, K extends number & keyof C["groups"], G extends C["groups"][K]> = Need<G>;
function f<C extends Config3>(x: C["groups"][number]) { const y: number = x; }
