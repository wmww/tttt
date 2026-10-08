// @strict: true
// link.test-d.tsx:4023: `const CustomLink: LinkComponent<C> = (props) => <CreatedLinkComponent preload="intent" {...props} />`.
// The arrow takes its contextual signature's type parameters, the very ones of the called signature, so the spread
// `{ preload } & Props<"a", TFrom, TTo>` has members identical to the parameter's, and tsc infers from those before they
// cancel out (inferFromMatchingTypes): TFrom to TFrom, not the defaults
interface Req<TFrom extends string> { to: TFrom & {} }
interface Opt<TFrom extends string> { to?: TFrom & {} }
type MakeReq<TFrom extends string, TTo extends string | undefined> = string extends TFrom
  ? string extends TTo ? Opt<TFrom> : Req<TFrom>
  : Opt<TFrom>
type Props<TComp, TFrom extends string, TTo extends string | undefined> = { comp?: TComp; preload?: string } & MakeReq<TFrom, TTo> & { hash?: string }
type Comp<TComp> = <const TFrom extends string = string, const TTo extends string | undefined = undefined>(props: Props<TComp, TFrom, TTo>) => [TFrom, TTo]
type Comp2<TComp> = <const TFrom extends string = string, const TTo extends string | undefined = undefined>(props: Props<TComp, TFrom, TTo>) => [TFrom, TTo]
declare const C: Comp<'a'>
const Custom: Comp2<'a'> = (props) => {
  const r1 = C({ preload: 'intent', ...props })
  return null!
}
const Custom4: Comp<'a'> = (props) => {
  const r4 = C({ ...props })
  return null!
}
function D<const TFrom extends string = string, const TTo extends string | undefined = undefined>(props: Props<'a', TFrom, TTo>): [TFrom, TTo] {
  const r5 = C({ preload: 'intent', ...props })
  const r6 = C({ ...props })
  const r7 = C(props)
  return null!
}
const CustomSame: Comp<'a'> = (props) => {
  const s1 = C({ preload: 'intent', ...props })
  return null!
}
const CustomSame2: Comp<'a'> = (props: Props<'a', string, undefined>) => {
  const s2 = C({ preload: 'intent', ...props })
  return null!
}
const CustomSame3: Comp<'a'> = function (props) {
  const s3 = C({ preload: 'intent', ...props })
  return null!
}
