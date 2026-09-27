// tsc: r4 is 2, r5 1, r7 2 (`[...T]['length']` stays deferred while T is generic: isGenericTupleType)
type BuildTuple<L extends number, Fill = unknown, T extends readonly unknown[] = []> = T['length'] extends L
	? T
	: BuildTuple<L, Fill, [...T, Fill]>;
type T4<A extends number, B extends number> = [...BuildTuple<A>, ...BuildTuple<B>]['length'];
type T5<A extends number> = [...BuildTuple<A>]['length'];
type T6<A extends number> = BuildTuple<A>['length'];
type C<A extends unknown[]> = [...A, ...A]['length'];
declare const r4: T4<1, 1>;
declare const r5: T5<1>;
declare const r6: T6<1>;
declare const r7: C<[1]>;
