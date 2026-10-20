// @strict: true
// `string & X` keeps X when X is the empty type literal however it was written (tsc's NoSupertypeReduction compares
// types with emptyTypeLiteralType): NonNullable<unknown> is lib's `{}`, so atproto's UnknownString stays `string & {}`
// and an identity check tells it from string (expect-type's `.not.toMatchObjectType`)
type UnknownString = string & NonNullable<unknown>
type U2 = string & {}
type Eq<L, R> = (<T>() => T extends (L & T) | T ? true : false) extends (<T>() => T extends (R & T) | T ? true : false) ? true : false;
type A1 = Eq<{ foo: UnknownString }, { foo: string }>;
type A2 = Eq<UnknownString, string>;
type A3 = Eq<U2, string>;
type A4 = Eq<{ foo: U2 }, { foo: string }>;
type A5 = Eq<{ foo: 'a' | UnknownString }, { foo: string }>;
declare const a: [A1, A2, A3, A4, A5];
type N = NonNullable<unknown>
type B1 = string & N
type B2 = string & NonNullable<unknown>
type N2 = unknown & {}
type B3 = string & N2
type B4 = string & (unknown & {})
type B5 = string & ({} & unknown)
declare const b: [B1, B2, B3, B4, B5, N];
