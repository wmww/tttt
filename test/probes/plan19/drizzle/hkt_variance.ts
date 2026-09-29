interface HKTBase { readonly config: unknown; readonly type: unknown }
interface Cfg { execute: unknown }
declare class PQ<T extends Cfg> { t: T }
type Kind<H extends HKTBase, C> = PQ<C & Cfg> & (H & { readonly config: C })["type"];
declare class Builder<TSel extends object | undefined, H extends HKTBase> {
    private fields: TSel;
    prepare(): Kind<H, { execute: TSel }>;
}
function f<H extends HKTBase>(a: Builder<undefined, H>, b: Builder<{ a: 1 } | undefined, H>) {
    const x: Builder<{ a: 1 } | undefined, H> = a;
    const y: Builder<undefined, H> = b;
}
