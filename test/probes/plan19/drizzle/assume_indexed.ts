// @strict: true
type Assume<T, U> = T extends U ? T : U;
declare class PQ<C> { c: C }
interface HKT { readonly config: unknown; readonly type: PQ<this["config"]> }
function f<H extends HKT, C>(x: Assume<(H & { readonly config: C })["type"], PQ<C>>, y: Assume<H["type"], PQ<C>>) {
    x;
    y;
}
