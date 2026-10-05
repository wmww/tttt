// @strict: true
// @target: es2022
interface Eff<A, E> { _a: A; _e: E }
class TestError1 { readonly _tag = "TestError1" }
declare const catchTag: <A, E, const K extends readonly [E extends { _tag: string } ? E["_tag"] : never, ...Array<E extends { _tag: string } ? E["_tag"] : never>], A1>(
    self: Eff<A, E>,
    ...args: [...tags: K, f: (e: Extract<NoInfer<E>, { _tag: K[number] }>) => A1]
  ) => Eff<A | A1, Exclude<E, { _tag: K[number] }>>
declare const e: Eff<number, TestError1>
export const a = catchTag(e, "TestError1", (x) => 1)
declare const c2: <A, E, const K extends readonly string[], A1>(self: Eff<A, E>, ...args: [...tags: K, f: (e: E) => A1]) => K
export const b = c2(e, "TestError1", (x) => 1)
