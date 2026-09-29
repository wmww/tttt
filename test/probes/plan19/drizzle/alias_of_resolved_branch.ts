type Assume<T, U> = T extends U ? T : U;
interface Cfg { execute: unknown }
declare class PQ<T extends Cfg> { t: T }
interface HKT { readonly config: unknown; readonly type: unknown }
interface HKTBase extends HKT { type: PQ<Assume<this['config'], Cfg>> }
function f<H extends HKTBase>(a: Assume<(H & { readonly config: { execute: "a" } })["type"], PQ<{ execute: "a" }>>) {
    const x: Assume<(H & { readonly config: { execute: string } })["type"], PQ<{ execute: string }>> = a;
}
type Kind<H extends HKT, C extends Cfg, A extends boolean> = [A] extends [true] ? Assume<(H & { readonly config: C })["type"], PQ<C>> : never;
function g<H extends HKTBase>(a: Kind<H, { execute: { x: 1 } }, true>, b: Kind<H, { execute: { x: 1 } | { x: 1 } }, true>) {
    const n: number = a;
}
