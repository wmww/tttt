// A generic call returning a function among the arguments of a variadic rest parameter waits for what the others infer
// (tsc's SkipGenericFunctions first inference): fn(...)'s `e` is TestError1 once K is inferred (effect dtslint Effect.tst 736)
// @target: es2015
// @lib: es2015
// @strict: true
interface Effect<out A, out E = never, out R = never> { readonly _A: () => A; readonly _E: () => E; readonly _R: () => R }
declare class YieldWrap<T> { #private; constructor(value: T) }
class TestError1 { readonly _tag = "TestError1" }
declare function hole<T>(): T
type NoInfer<A> = [A][A extends any ? 0 : never]
type NonEmptyReadonlyArray<A> = readonly [A, ...Array<A>]
declare const catchTag: {
  <E, const K extends NonEmptyReadonlyArray<E extends { _tag: string } ? E["_tag"] : never>, A1, E1, R1>(
    ...args: [...tags: K, f: (e: Extract<NoInfer<E>, { _tag: K[number] }>) => Effect<A1, E1, R1>]
  ): <A, R>(self: Effect<A, E, R>) => Effect<A | A1, Exclude<E, { _tag: K[number] }> | E1, R | R1>
  <A, E, R, const K extends NonEmptyReadonlyArray<E extends { _tag: string } ? E["_tag"] : never>, A1, E1, R1>(
    self: Effect<A, E, R>,
    ...args: [...tags: K, f: (e: Extract<NoInfer<E>, { _tag: K[number] }>) => Effect<A1, E1, R1>]
  ): Effect<A | A1, Exclude<E, { _tag: K[number] }> | E1, R | R1>
}
type Gen = {
  <Eff extends YieldWrap<Effect<any, any, any>>, AEff, Args extends Array<any>>(
    body: (...args: Args) => Generator<Eff, AEff, never>
  ): (...args: Args) => Effect<AEff, [Eff] extends [never] ? never : [Eff] extends [YieldWrap<Effect<infer _A, infer E, infer _R>>] ? E : never, never>
  <Eff extends YieldWrap<Effect<any, any, any>>, AEff, Args extends Array<any>, A extends Effect<any, any, any>>(
    body: (...args: Args) => Generator<Eff, AEff, never>,
    a: (_: Effect<AEff>, ...args: NoInfer<Args>) => A
  ): (...args: Args) => A
}
declare const fn: Gen
declare function pipe<A, B>(a: A, ab: (a: A) => B): B
export const r = pipe(
  hole<Effect<number, TestError1 | Error>>(),
  catchTag("TestError1", fn(function*(e) { return 1 }))
)
