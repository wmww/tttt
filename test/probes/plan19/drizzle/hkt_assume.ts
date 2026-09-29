// @strict: true
type Assume<T, U> = T extends U ? T : U;
type Equal<X, Y> = (<T>() => T extends X ? 1 : 2) extends (<T>() => T extends Y ? 1 : 2) ? true : false;
interface Cfg { execute: unknown }
declare class PQ<T extends Cfg> { t: T }
interface HKT { readonly $brand: 'HKT'; readonly config: unknown; readonly type: unknown }
interface HKTBase extends HKT { type: PQ<Assume<this['config'], Cfg>> }
type Kind<TKind extends HKT, TConfig extends Cfg, TAssume extends boolean = false> = Equal<TAssume, true> extends true
    ? Assume<(TKind & { readonly config: TConfig })['type'], PQ<TConfig>>
    : (TKind & { readonly config: TConfig })['type'];
function f<H extends HKTBase>(k: Kind<H, Cfg & { execute: { a: 1 } }, true>) {
    const n: number = k;
}
