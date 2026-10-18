// @strict: true
// @noTypesAndSymbols: true
// @noEmit: true
// drizzle-orm's query builders (MySqlUpdateBase, the clidiff drizzle elaboration): a deferred reference is related as
// the plain reference of its arguments (tsgo's getNormalizedType), so `Dynamic<this>` of every `this` along the way is
// the one pair Base<HKT2, true> -> Base<HKT, true>, deeply nested once and then on the maybe stack: the structural
// comparison passes and the error is the variance one, not 'where().$dynamic()'
interface HKT { readonly brand: 'HKT'; readonly type: unknown; }
interface HKT2 extends HKT { type: string[]; }

type Without<T extends AnyBase, D extends boolean, K extends keyof T & string> = D extends true ? T : Omit<
    Base<T['_']['q'], D, T['_']['excluded'] | K>,
    T['_']['excluded'] | K
>;
type Dynamic<T extends AnyBase> = Base<T['_']['q'], true>;
type AnyBase = Base<any, any, any>;

declare class Base<Q extends HKT, D extends boolean = false, E extends string = never> {
    readonly _: { readonly q: Q; readonly d: D; readonly excluded: E };
    f: (x: Q['brand']) => void;
    where(): Without<this, D, 'where'>;
    limit(): Without<this, D, 'limit'>;
    $dynamic(): Dynamic<this>;
}

declare let x: Base<HKT2>;
let y: Base<HKT> = x;
