// @declaration: true
// @declarationMap: true
// @strict: true
// A template literal type's type reused in an instantiated signature is mapped at its start: the type parameter
// reference of the returned generic function (a fresh clone, printed by name) has no mapping of its own (effect's
// matcher `tagStartsWith = discriminatorStartsWith("_tag")`)
export const discriminatorStartsWith = <D extends string>(field: D) =>
<P extends string, Fn extends [D, `${P}${string}`]>(pattern: P, f: Fn) => [field, pattern, f] as const
export const tagStartsWith = discriminatorStartsWith("_tag")
