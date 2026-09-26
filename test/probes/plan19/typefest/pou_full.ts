// @strict: true
// @target: esnext
type Primitive = null | undefined | string | number | boolean | symbol | bigint;
type BuiltIns = Primitive | void | Date | RegExp;
type IsNever<T> = [T] extends [never] ? true : false;
type IsNull<T> = [T] extends [null] ? true : false;
type IsUnknown<T> = (unknown extends T ? IsNull<T> extends false ? true : false : false);
type IfUnknown<T, TypeIfUnknown = true, TypeIfNotUnknown = false> = (IsUnknown<T> extends true ? TypeIfUnknown : TypeIfNotUnknown);
type IsLiteral<K> = K extends string ? (string extends K ? false : true) : K extends number ? (number extends K ? false : true) : false;
type LiteralKeyOf<T> = keyof {[K in keyof T as IsLiteral<K> extends true ? K : never]-?: never};
type OmitIndexSignature<ObjectType> = { [KeyType in keyof ObjectType as {} extends Record<KeyType, unknown> ? never : KeyType]: ObjectType[KeyType]; };
type PickIndexSignature<ObjectType> = { [KeyType in keyof ObjectType as {} extends Record<KeyType, unknown> ? KeyType : never]: ObjectType[KeyType]; };
type Simplify<T> = {[KeyType in keyof T]: T[KeyType]} & {};
type SimpleMerge<Destination, Source> = { [Key in keyof Destination as Key extends keyof Source ? never : Key]: Destination[Key]; } & Source;
type Merge<Destination, Source> = Simplify<SimpleMerge<PickIndexSignature<Destination>, PickIndexSignature<Source>> & SimpleMerge<OmitIndexSignature<Destination>, OmitIndexSignature<Source>>>;
type PartialOnUndefinedDeepOptions = { readonly recurseIntoArrays?: boolean; };
type PartialOnUndefinedDeep<T, Options extends PartialOnUndefinedDeepOptions = {}> = T extends Record<any, any> | undefined
	? {[KeyType in keyof T as undefined extends T[KeyType] ? IfUnknown<T[KeyType], never, KeyType> : never]?: PartialOnUndefinedDeepValue<T[KeyType], Options>} extends infer U
		? Merge<{[KeyType in keyof T as KeyType extends LiteralKeyOf<U> ? never : KeyType]: PartialOnUndefinedDeepValue<T[KeyType], Options>}, U>
		: never
	: T;
type PartialOnUndefinedDeepValue<T, Options extends PartialOnUndefinedDeepOptions> = T extends BuiltIns | ((...arguments_: any[]) => unknown)
	? T
	: T extends ReadonlyArray<infer U>
		? Options['recurseIntoArrays'] extends true
			? U[] extends T
				? readonly U[] extends T
					? ReadonlyArray<PartialOnUndefinedDeep<U, Options>>
					: Array<PartialOnUndefinedDeep<U, Options>>
				: PartialOnUndefinedDeep<{[Key in keyof T]: PartialOnUndefinedDeep<T[Key], Options>}, Options>
			: T
		: T extends Record<any, any> | undefined
			? PartialOnUndefinedDeep<T, Options>
			: unknown;
declare function expectType<T>(v: T): void;
declare function expectAssignable<T>(v: T): void;

type TestingType = {
	function: (() => void) | undefined;
	object: {objectKey: 1} | undefined;
	objectDeep: {
		subObject: string | undefined;
	};
	string: string | undefined;
	union: 'test1' | 'test2' | undefined;
	number: number | undefined;
	boolean: boolean | undefined;
	date: Date | undefined;
	regexp: RegExp | undefined;
	symbol: symbol | undefined;
	null: null | undefined;
	record: Record<string, any> | undefined;
	map: Map<string, string> | undefined;
	set: Set<string> | undefined;
	array1: any[] | undefined;
	array2: Array<{propertyA: string; propertyB: number | undefined}> | undefined;
	readonly1: readonly any[] | undefined;
	readonly2: ReadonlyArray<{propertyA: string; propertyB: number | undefined}> | undefined;
	readonly readonlyProperty: string | undefined;
	tuple: ['test1', {propertyA: string; propertyB: number | undefined}] | undefined;
};
declare const indexType: {[k: string]: string | undefined; propertyA: string; propertyB: string | undefined};
declare const indexTypeUnknown: {[k: string]: unknown; propertyA: string; propertyB: number | undefined};

// Default behavior, without recursion into arrays/tuples
declare const foo: PartialOnUndefinedDeep<TestingType>;
expectAssignable<{
	function?: TestingType['function'];
	object?: TestingType['object'];
	objectDeep: {
		subObject?: TestingType['objectDeep']['subObject'];
	};
	string?: TestingType['string'];
	union?: TestingType['union'];
	number?: TestingType['number'];
	boolean?: TestingType['boolean'];
	date?: TestingType['date'];
	regexp?: TestingType['regexp'];
	symbol?: TestingType['symbol'];
	null?: TestingType['null'];
	record?: TestingType['record'];
	map?: TestingType['map'];
	set?: TestingType['set'];
	array1?: TestingType['array1'];
	array2?: TestingType['array2'];
	readonly1?: TestingType['readonly1'];
	readonly2?: TestingType['readonly2'];
	readonly readonlyProperty?: TestingType['readonlyProperty'];
	tuple?: TestingType['tuple'];
}>(foo);

declare const indexTypeWithoutRecursion: PartialOnUndefinedDeep<typeof indexType>;
declare const indexTypeUnknownWithoutRecursion: PartialOnUndefinedDeep<typeof indexTypeUnknown>;
expectType<{[k: string]: string | undefined; propertyA: string; propertyB?: string | undefined}>(indexTypeWithoutRecursion);
expectType<{[k: string]: unknown; propertyA: string; propertyB?: number | undefined}>(indexTypeUnknownWithoutRecursion);

// With recursion into arrays/tuples activated
declare const bar: PartialOnUndefinedDeep<TestingType, {recurseIntoArrays: true}>;
expectAssignable<{
	function?: TestingType['function'];
	object?: TestingType['object'];
	objectDeep: {
		subObject?: TestingType['objectDeep']['subObject'];
	};
	string?: TestingType['string'];
	union?: TestingType['union'];
	number?: TestingType['number'];
	boolean?: TestingType['boolean'];
	date?: TestingType['date'];
	regexp?: TestingType['regexp'];
	symbol?: TestingType['symbol'];
	null?: TestingType['null'];
	record?: TestingType['record'];
	map?: TestingType['map'];
	set?: TestingType['set'];
	array1?: TestingType['array1'];
	array2?: Array<{propertyA: string; propertyB?: number | undefined}> | undefined;
	readonly1?: TestingType['readonly1'];
	readonly2?: ReadonlyArray<{propertyA: string; propertyB?: number | undefined}> | undefined;
	readonly readonlyProperty?: TestingType['readonlyProperty'];
	tuple?: ['test1', {propertyA: string; propertyB?: number | undefined}] | undefined;
}>(bar);

declare const indexTypeWithRecursion: PartialOnUndefinedDeep<typeof indexType, {recurseIntoArrays: true}>;
declare const indexTypeUnknownWithRecursion: PartialOnUndefinedDeep<typeof indexTypeUnknown, {recurseIntoArrays: true}>;
expectType<{[k: string]: string | undefined; propertyA: string; propertyB?: string | undefined}>(indexTypeWithRecursion);
expectType<{[k: string]: unknown; propertyA: string; propertyB?: number | undefined}>(indexTypeUnknownWithRecursion);
