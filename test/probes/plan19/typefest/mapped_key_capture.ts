// @strict: true
// R20: the inner X<T[K]> (reached through CPD) got a mapper composed from the outer X instance, K -> "object"
// included; its template must see its own K. tsc: no errors
declare const sym: unique symbol;
type Keys<Base, Condition> = { [Key in keyof Base]-?: Base[Key] extends Condition ? Key : never; }[keyof Base];
type CPD<Type> = Keys<X<Type>, typeof sym | {a?: never}>;
type X<Type> = {
	[Key in keyof Type]: Type[Key] extends string ? Type[Key] : Type[Key] extends object ? CPD<Type[Key]> : typeof sym
};
type Example = { string: string; object: { string: string; }; };
declare const t: X<Example>["object"]; const u: 1 = t;
