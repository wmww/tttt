// @strict: true
type CSD<Type, ExcludeType = never, IncludeType = unknown> = Type extends ExcludeType ? Type
	: Type extends IncludeType ? {[TypeKey in keyof Type]: CSD<Type[TypeKey], ExcludeType, IncludeType>} : Type;
type Ex = { sb: string | boolean; n: number; o: { sb: string | boolean } };
declare const a: CSD<Ex, never, Record<PropertyKey, unknown>>;
declare const b: CSD<string | boolean, never, Record<PropertyKey, unknown>>;
declare const c: CSD<{ sb: string | boolean }, never, Record<PropertyKey, unknown>>;
declare const d: CSD<{ sb: string | boolean }>;
declare const e: CSD<{ sb: string | number }, never, Record<PropertyKey, unknown>>;
