// type-fest's FixedLengthArray: `keyof` constraint steps push only the target on the deep nesting stack (plan 61)
// @strict: true

type IsAny<T> = 0 extends 1 & NoInfer<T> ? true : false;
type IsNever<T> = [T] extends [never] ? true : false;
type INAON<T, C extends {ifNot: unknown; ifAny?: unknown; ifNever?: unknown}> =
    IsAny<T> extends true ? 'ifAny' extends keyof C ? C['ifAny'] : any
    : IsNever<T> extends true ? 'ifNever' extends keyof C ? C['ifNever'] : never
    : C['ifNot'];
type TO<L extends number, F = unknown> = INAON<L, {ifNot: _TO<L, F>; ifAny: F[]; ifNever: []}>;
type _TO<L extends number, F> = number extends L ? F[] : BT<`${L}`, F>;
type BT<L extends string, F, A extends unknown[] = []> = L extends `${infer X extends '0' | '1'}${infer R}` ? BT<R, F, [...A, ...A, ...(X extends '1' ? [F] : [])]> : A;
type Except<O, K extends keyof O> = {[P in keyof O as P extends K ? never : P]: O[P]};
type FLA<E, L extends number> = Except<TO<L, E>, 'push' | number | 'length'>;
