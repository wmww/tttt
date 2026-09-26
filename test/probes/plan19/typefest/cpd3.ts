// @strict: true
// @target: esnext
type Primitive = null | undefined | string | number | boolean | symbol | bigint;
type BuiltIns = Primitive | void | Date | RegExp;
type NonRecursiveType = BuiltIns | Function | (new (...arguments_: any[]) => unknown);
type UnknownArray = readonly unknown[];
type IsPlainObject<T> =
	T extends NonRecursiveType | UnknownArray | ReadonlyMap<unknown, unknown> | ReadonlySet<unknown>
		? false : T extends object ? true : false;
type IsEqual<A, B> = (<G>() => G extends A ? 1 : 2) extends (<G>() => G extends B ? 1 : 2) ? true : false;
type IsNever<T> = [T] extends [never] ? true : false;
type IfNever<T, TypeIfNever = true, TypeIfNotNever = false> = (IsNever<T> extends true ? TypeIfNever : TypeIfNotNever);
type ConditionalKeys<Base, Condition> = {
	[Key in keyof Base]-?: Base[Key] extends Condition ? IfNever<Base[Key], IfNever<Condition, Key, never>, Key> : never;
}[keyof Base];
type Filter<KeyType, ExcludeType> = IsEqual<KeyType, ExcludeType> extends true ? never : (KeyType extends ExcludeType ? never : KeyType);
type Except<ObjectType, KeysType extends keyof ObjectType> = {
	[KeyType in keyof ObjectType as Filter<KeyType, KeysType>]: ObjectType[KeyType];
} & {};
type ConditionalExcept<Base, Condition> = Except<Base, ConditionalKeys<Base, Condition>>;
type ConditionalSimplifyDeep<Type, ExcludeType = never, IncludeType = unknown> = Type extends ExcludeType ? Type
	: Type extends IncludeType ? {[TypeKey in keyof Type]: ConditionalSimplifyDeep<Type[TypeKey], ExcludeType, IncludeType>} : Type;
type UnknownRecord = Record<PropertyKey, unknown>;
declare const emptyObjectSymbol: unique symbol;
type EmptyObject = {[emptyObjectSymbol]?: never};
declare const conditionalPickDeepSymbol: unique symbol;
type AssertCondition<Type, Condition> = Type extends Condition ? true : false;
type ConditionalPickDeep<Type, Condition> = ConditionalSimplifyDeep<ConditionalExcept<{
	[Key in keyof Type]: AssertCondition<Type[Key], Condition> extends true
		? Type[Key]
		: IsPlainObject<Type[Key]> extends true
			? ConditionalPickDeep<Type[Key], Condition>
			: typeof conditionalPickDeepSymbol;
}, (typeof conditionalPickDeepSymbol | undefined) | EmptyObject>, never, UnknownRecord>;

type Example = { n: number; string: string; object: { string: string; }; };
type M<Type, Condition> = {
	[Key in keyof Type]: AssertCondition<Type[Key], Condition> extends true
		? Type[Key]
		: IsPlainObject<Type[Key]> extends true
			? ConditionalPickDeep<Type[Key], Condition>
			: typeof conditionalPickDeepSymbol;
};
type K = (typeof conditionalPickDeepSymbol | undefined) | EmptyObject;
declare const e1: ConditionalExcept<M<Example, string>, K>;
declare const e2: Except<M<Example, string>, "n">;
declare const e3: ConditionalSimplifyDeep<Except<M<Example, string>, "n">, never, UnknownRecord>;
declare const e4: ConditionalSimplifyDeep<ConditionalExcept<M<Example, string>, K>, never, UnknownRecord>;
