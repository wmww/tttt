// @strict: true
// @noTypesAndSymbols: true
// @noEmit: true
// drizzle-orm's query builders (MySqlUpdateBase): a class or interface reference written in a type annotation is its
// own recursion identity (tsgo's ObjectFlags.FromTypeNode), so `x`'s Base<HKT2> doesn't count towards Base's deep
// nesting: where()'s Omit<Base<...>> and its $dynamic() aren't deeply nested, and that is where the error is
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
    $dynamic(): Dynamic<this>;
}

declare let x: Base<HKT2>;
let y: Base<HKT> = x;
