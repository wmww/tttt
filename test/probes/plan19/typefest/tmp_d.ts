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
type ConditionalPickDeepOptions = { condition?: 'extends' | 'equality'; };
type AssertCondition<Type, Condition, Options extends ConditionalPickDeepOptions> = IsEqual<Type, Condition> extends 5 ? 1 : Type extends Condition ? true : false;
type ConditionalPickDeep<Type, Condition, Options extends ConditionalPickDeepOptions = {}> = ConditionalSimplifyDeep<ConditionalExcept<{
	[Key in keyof Type]: AssertCondition<Type[Key], Condition, Options> extends true
		? Type[Key]
		: IsPlainObject<Type[Key]> extends true
			? ConditionalPickDeep<Type[Key], Condition, Options>
			: typeof conditionalPickDeepSymbol;
}, (typeof conditionalPickDeepSymbol | undefined) | EmptyObject>, never, UnknownRecord>;
declare function expectType<T>(v: T): void;
type Example = { object: { string: string } };
declare const p: ConditionalPickDeep<Example, string>;
const q: {object: {string: string}} = p;
