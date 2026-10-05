// @strict: true
// @target: es2022
interface Arbitrary<T> { _t: T }
declare function asyncProperty<Ts extends [unknown, ...unknown[]]>(...args: [...arbitraries: {
    [K in keyof Ts]: Arbitrary<Ts[K]>;
}, predicate: (...args: Ts) => Promise<boolean | void>]): Ts;
declare const a: Arbitrary<number>
declare const b: Arbitrary<string[]>
export const r = asyncProperty(a, b, async (n, as) => { n.toFixed(); as.length })
