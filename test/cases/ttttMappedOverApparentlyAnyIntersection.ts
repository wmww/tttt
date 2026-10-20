// @strict: true
// A homomorphic mapped type over an intersection with a member whose constraint is any (here an unresolved import's
// error type would do too) is a string index signature only: the intersection's apparent type is any (tsc's
// getApparentTypeOfIntersectionType), so `$type` isn't one of its keys. Its relations with an object type both fail,
// which atproto's lex-schema `build()` overloads report as incompatible with their implementation (TS2394)
interface Validator<TInput = unknown, TOutput = TInput> { readonly ['__lex']: { input: TInput; output: TOutput } }
type InferInput<V extends Validator> = V['__lex']['input']
type InferOutput<V extends Validator> = V['__lex']['output']
type Simplify<T> = { [K in keyof T]: T[K] } & NonNullable<unknown>
type $Typed<V, T extends string = string> = Simplify<V & { $type: T }>
declare function $typed<V extends { $type?: unknown }, T extends string>(value: V, $type: T): $Typed<V, T>

export class X<const TType extends string, const TShape extends Validator<any>> {
  declare $type: TType
  build(input: Omit<InferOutput<TShape>, '$type'>): $Typed<InferOutput<TShape>, TType>
  build(input: Omit<InferInput<TShape>, '$type'>): $Typed<InferInput<TShape>, TType>
  build(input: Record<string, unknown>) { return $typed(input, this.$type) }
}

function f<const TType extends string, const TShape extends Validator<any>>(
  a: $Typed<InferOutput<TShape>, TType>, b: $Typed<Record<string, unknown>, TType>) {
  const t = a.$type;
  let s1: { $type: TType } = a;
  let s2: { [x: string]: unknown } = a;
  let x1: typeof b = a;
  let x2: typeof a = b;
}
