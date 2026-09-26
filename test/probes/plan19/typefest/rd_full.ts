// @strict: true
// @target: esnext
type Primitive = null | undefined | string | number | boolean | symbol | bigint;
type BuiltIns = Primitive | void | Date | RegExp;
type HasMultipleCallSignatures<T extends (...arguments_: any[]) => unknown> =
	T extends {(...arguments_: infer A): unknown; (...arguments_: infer B): unknown}
		? B extends A ? A extends B ? false : true : true : false;
type ReadonlyDeep<T> = T extends BuiltIns
	? T
	: T extends new (...arguments_: any[]) => unknown
		? T
		: T extends (...arguments_: any[]) => unknown
			? {} extends ReadonlyObjectDeep<T>
				? T
				: HasMultipleCallSignatures<T> extends true
					? T
					: ((...arguments_: Parameters<T>) => ReturnType<T>) & ReadonlyObjectDeep<T>
			: T extends Readonly<ReadonlyMap<infer KeyType, infer ValueType>>
				? ReadonlyMapDeep<KeyType, ValueType>
				: T extends Readonly<ReadonlySet<infer ItemType>>
					? ReadonlySetDeep<ItemType>
					: T extends readonly [] | readonly [...never[]]
						? readonly []
						: T extends readonly [infer U, ...infer V]
							? readonly [ReadonlyDeep<U>, ...ReadonlyDeep<V>]
							: T extends readonly [...infer U, infer V]
								? readonly [...ReadonlyDeep<U>, ReadonlyDeep<V>]
								: T extends ReadonlyArray<infer ItemType>
									? ReadonlyArray<ReadonlyDeep<ItemType>>
									: T extends object
										? ReadonlyObjectDeep<T>
										: unknown;
type ReadonlyMapDeep<KeyType, ValueType> = {} & Readonly<ReadonlyMap<ReadonlyDeep<KeyType>, ReadonlyDeep<ValueType>>>;
type ReadonlySetDeep<ItemType> = {} & Readonly<ReadonlySet<ReadonlyDeep<ItemType>>>;
type ReadonlyObjectDeep<ObjectType extends object> = {
	readonly [KeyType in keyof ObjectType]: ReadonlyDeep<ObjectType[KeyType]>
};
type JsonObject = {[Key in string]: JsonValue} & {[Key in string]?: JsonValue | undefined};
type JsonArray = JsonValue[] | readonly JsonValue[];
type JsonPrimitive = string | number | boolean | null;
type JsonValue = JsonPrimitive | JsonObject | JsonArray;

const data = {
	json: [{x: true}] as JsonValue,
};
const readonlyData: ReadonlyDeep<typeof data> = data;
