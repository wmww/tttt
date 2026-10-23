// @strict: true
// @noTypesAndSymbols: true
// Inferring between two generic signatures matches `CheckDependencies<I2, NoInfer<It2>>` with the target's
// `CheckDependencies<I, NoInfer<It>>` as identical: both check types normalize to never (tsc's getNormalizedType
// simplifies `Exclude<Services<I>, ListOutput<It>>` before the identity relation), so I is inferred as I2 and not as
// `I2 & CheckLayer<...>` from the naked member of the layer intersection (opencode util/src/effect/layer-node.ts)
interface Layer<in ROut, out E = never, out RIn = never> { readonly _a: (_: ROut) => void; readonly _e: () => E; readonly _r: () => RIn }
interface AnyLayer {}
type Services<T extends AnyLayer> = T extends infer L ? L extends Layer<infer _ROut, infer _E, infer _RIn> ? _RIn : never : never

type GraphList = readonly [] | readonly [unknown, ...unknown[]]
type ListOutput<Items extends readonly unknown[], A = never> = [Items] extends [readonly [infer Head, ...infer Tail extends readonly unknown[]]] ? ListOutput<Tail, A | Head> : A
type Tag<N extends string = string> = N & { readonly __tag: unique symbol }
type CheckDependencies<Implementation extends AnyLayer, Items extends GraphList> = [
  Exclude<Services<Implementation>, ListOutput<Items>>,
] extends [never]
  ? unknown
  : { readonly "Missing dependencies": Exclude<Services<Implementation>, ListOutput<Items>> }
type Identity =
  | { readonly name: string; readonly service?: never }
type CheckLayer<Implementation> = [Implementation] extends [Layer<never, unknown, unknown>]
  ? unknown
  : { readonly "Layer contract must be preserved": Implementation }
type TagInput<T> = { readonly tag: T } | ([T] extends [undefined] ? { readonly tag?: undefined } : never)
type MakeInput<Implementation extends AnyLayer, Items extends GraphList, T extends Tag | undefined> = Identity &
  TagInput<T> & {
    readonly layer: Implementation & CheckLayer<NoInfer<Implementation>>
    readonly deps: Items & CheckDependencies<Implementation, NoInfer<Items>>
  }
type DistributiveOmit<A, K extends PropertyKey> = A extends unknown ? Omit<A, K> : never

export type TagMaker<Name extends string> = <const Implementation extends AnyLayer, const Items extends GraphList>(
  input: DistributiveOmit<MakeInput<Implementation, Items, Tag<Name>>, "tag">,
) => void

export interface Tags<Config extends string> {
  readonly make: <Name extends Config>(name: Name) => TagMaker<Name>
}
export function tags<Config extends string>(): Tags<Config> {
  return {
    make:
      <Name extends Config>(name: Name) =>
      <const Implementation extends AnyLayer, const Items extends GraphList>(
        input: DistributiveOmit<MakeInput<Implementation, Items, Tag<Name>>, "tag">,
      ) => {},
  }
}
