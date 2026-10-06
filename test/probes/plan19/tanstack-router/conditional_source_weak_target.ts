// @strict: true
// A conditional source relates its constraints to an intersection target's member without IntersectionState.Target
// (tsc passes none there), so a weak member is checked for common properties (f); when the members fail, the
// constraints are related to the whole target (structuredTypeRelatedToWorker goes on for an instantiable source:
// vue's shallowRef, g). Branches of a conditional target push only the target (RecursionFlags.Target), else
// `Opt<TFrom>` against `MakeReq` looked infinitely expanding and was assumed related
interface Req<TFrom extends string> { to: TFrom & {} }
interface Opt<TFrom extends string> { to?: TFrom & {} }
type MakeReq<TFrom extends string, TTo extends string | undefined> = string extends TFrom
  ? string extends TTo ? Opt<TFrom> : Req<TFrom>
  : Opt<TFrom>
function f<TFrom extends string, TTo extends string | undefined>(m: MakeReq<TFrom, TTo>) {
  const p: { comp?: 'a' } & MakeReq<TFrom, TTo> = m
}
declare const RefSymbol: unique symbol
declare const ShallowRefMarker: unique symbol
interface Ref<T = any, S = T> { get value(): T; set value(_: S); [RefSymbol]: true }
type ShallowRef<T = any, S = T> = Ref<T, S> & { [ShallowRefMarker]?: true }
type IfAny<T, Y, N> = 0 extends 1 & T ? Y : N
declare function shallowRef<T>(value: T): Ref extends T ? T extends Ref ? IfAny<T, ShallowRef<T>, T> : ShallowRef<T> : ShallowRef<T>
declare function expectType<T>(value: T): void
function g<T extends { name: string }>() {
  const r = {} as T
  expectType<ShallowRef<T>>(shallowRef(r))
}
