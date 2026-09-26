// @strict: true
// @target: esnext
type Primitive = null | undefined | string | number | boolean | symbol | bigint;
type BuiltIns = Primitive | void | Date | RegExp;
type IsNever<T> = [T] extends [never] ? true : false;
type IsNull<T> = [T] extends [null] ? true : false;
type IsUnknown<T> = (unknown extends T ? IsNull<T> extends false ? true : false : false);
type IfUnknown<T, TypeIfUnknown = true, TypeIfNotUnknown = false> = (IsUnknown<T> extends true ? TypeIfUnknown : TypeIfNotUnknown);
type IsNotFalse<T extends boolean> = [T] extends [false] ? false : true;
type IsPrimitive<T> = [T] extends [Primitive] ? true : false;
type LiteralCheck<T, LiteralType extends Primitive> = (IsNever<T> extends false ? [T] extends [LiteralType & infer U] ? [U] extends [LiteralType] ? [LiteralType] extends [U] ? false : true : false : false : false);
type LiteralChecks<T, LiteralUnionType> = (IsNotFalse<LiteralUnionType extends Primitive ? LiteralCheck<T, LiteralUnionType> : never>);
type IsLiteral<T> = IsPrimitive<T> extends true ? IsNotFalse<LiteralCheck<T, string> | LiteralChecks<T, number | bigint> | LiteralCheck<T, boolean> | LiteralCheck<T, symbol>> : false;
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
declare const indexType: {[k: string]: string | undefined; propertyA: string; propertyB: string | undefined};
declare function expectAssignable<T>(v: T): void;
type Tup = ['test1', {propertyA: string; propertyB: number | undefined}];
declare const t1: PartialOnUndefinedDeepValue<Tup, {recurseIntoArrays: true}>;
declare const t2: PartialOnUndefinedDeep<Tup, {recurseIntoArrays: true}>;
declare const t3: {[Key in keyof Tup]: PartialOnUndefinedDeep<Tup[Key], {recurseIntoArrays: true}>};
expectAssignable<['test1', {propertyA: string; propertyB?: number | undefined}]>(t1);
