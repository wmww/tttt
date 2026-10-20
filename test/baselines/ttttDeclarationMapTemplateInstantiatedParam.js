//// [test/cases/ttttDeclarationMapTemplateInstantiatedParam.ts] ////

//// [ttttDeclarationMapTemplateInstantiatedParam.ts]
// A template literal type's type reused in an instantiated signature is mapped at its start: the type parameter
// reference of the returned generic function (a fresh clone, printed by name) has no mapping of its own (effect's
// matcher `tagStartsWith = discriminatorStartsWith("_tag")`)
export const discriminatorStartsWith = <D extends string>(field: D) =>
<P extends string, Fn extends [D, `${P}${string}`]>(pattern: P, f: Fn) => [field, pattern, f] as const
export const tagStartsWith = discriminatorStartsWith("_tag")


//// [ttttDeclarationMapTemplateInstantiatedParam.js]
// A template literal type's type reused in an instantiated signature is mapped at its start: the type parameter
// reference of the returned generic function (a fresh clone, printed by name) has no mapping of its own (effect's
// matcher `tagStartsWith = discriminatorStartsWith("_tag")`)
export const discriminatorStartsWith = (field) => (pattern, f) => [field, pattern, f];
export const tagStartsWith = discriminatorStartsWith("_tag");


//// [ttttDeclarationMapTemplateInstantiatedParam.d.ts]
export declare const discriminatorStartsWith: <D extends string>(field: D) => <P extends string, Fn extends [D, `${P}${string}`]>(pattern: P, f: Fn) => readonly [D, P, Fn];
export declare const tagStartsWith: <P extends string, Fn extends ["_tag", `${P}${string}`]>(pattern: P, f: Fn) => readonly ["_tag", P, Fn];
//# sourceMappingURL=ttttDeclarationMapTemplateInstantiatedParam.d.ts.map