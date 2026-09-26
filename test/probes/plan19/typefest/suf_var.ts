// @strict: true
type UnknownArray = readonly unknown[];
type IsNever<T> = [T] extends [never] ? true : false;
type TupleLength<T extends UnknownArray> = T extends unknown ? number extends T['length'] ? never : T['length'] : never;
type UnionMin<N extends number, T extends UnknownArray = []> = T['length'] extends N ? T['length'] : UnionMin<N, [...T, unknown]>;
type UnionMax<N extends number, T extends UnknownArray = []> = IsNever<N> extends true ? T['length'] : T['length'] extends N ? UnionMax<Exclude<N, T['length']>, T> : UnionMax<N, [...T, unknown]>;
type StaticPartOfArray<T extends UnknownArray, Result extends UnknownArray = []> =
	T extends unknown ? number extends T['length'] ? T extends readonly [infer U, ...infer V] ? StaticPartOfArray<V, [...Result, U]> : Result : T : never;
type VariablePartOfArray<T extends UnknownArray> = T extends unknown ? T extends readonly [...StaticPartOfArray<T>, ...infer U] ? U : [] : never;
type V1 = VariablePartOfArray<[number, ...string[]]>;
type V2 = VariablePartOfArray<boolean[]>;
type S1 = StaticPartOfArray<[number, ...string[]]>;
type W1 = [number, ...string[]] extends readonly [...[number], ...infer U] ? U : 'no';
type W2<T> = T extends readonly [...[number], ...infer U] ? U : 'no';
type W3 = W2<[number, ...string[]]>;
