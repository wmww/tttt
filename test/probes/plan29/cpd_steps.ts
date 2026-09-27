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
type ConditionalPickDeep<Type, Condition, Options = {}> = ConditionalSimplifyDeep<ConditionalExcept<{
	[Key in keyof Type]: AssertCondition<Type[Key], Condition> extends true
		? Type[Key]
		: IsPlainObject<Type[Key]> extends true
			? ConditionalPickDeep<Type[Key], Condition, Options>
type Example = { n: number; string: string; object: { string: string; }; };
type Inner = {
	[Key in keyof Example]: AssertCondition<Example[Key], string> extends true
		? Example[Key]
		: IsPlainObject<Example[Key]> extends true
			? ConditionalPickDeep<Example[Key], string, {}>
			: typeof conditionalPickDeepSymbol;
};
declare const inner: Inner;
declare const innerObj: Inner["object"];
declare const keys: ConditionalKeys<Inner, (typeof conditionalPickDeepSymbol | undefined) | EmptyObject>;
declare const exc: ConditionalExcept<Inner, (typeof conditionalPickDeepSymbol | undefined) | EmptyObject>;
declare const sub: ConditionalPickDeep<{ string: string; }, string>;
declare const isPlain: IsPlainObject<{ string: string; }>;
