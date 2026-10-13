// @strict: true
// mikro-orm's `$in: raw()`: the contextual type goes through the immediate distributive constraint (plan 61)
type OperatorMap<T> = { $in?: readonly T[] };
declare function r<R = "dflt">(): R;
type EP<T> = T extends (infer U)[] ? NonNullable<U> : NonNullable<T>;
type EQ<T> = T extends object ? OperatorMap<T> : number;
type MU<T> = [T] extends [object] ? T extends string ? T : { [K in keyof T]: T[K] } : T;
declare function g2<E extends object>(w: { b?: EQ<EP<MU<E>["books"]>> }): void;
g2({ b: { $in: r() } });
