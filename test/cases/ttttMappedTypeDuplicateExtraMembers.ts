// @target: es2015
// @strict: true
// A mapped type's members after the mapping (its `}` gone: TS7061 recovery) are never checked (tsc's checkMappedType),
// so no TS2300 for two of one name, and the binder merges them into one symbol (seed 20261113's genericMappedTypeAsClause;
// ttttMappedTypeExtraMembers is the TS7008 case)
type Model = { a: string; b: number };
type MappedModel<Suffix extends string> = {
    [K in keyof Model as `${K}${Suffix}`]: Model[K];

const foo1: MappedModel<'Foo'> = { aFoo: 'test', bFoo: 42 };
const foo2: MappedModel<'Foo'> = { bFoo: 'bar' };
