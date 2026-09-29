// @strict: true
// @lib: es2020,dom
declare class JSHandle<T = unknown> { move: T }
type HandleOr<T> = JSHandle<T> | T
type FlattenHandle<T> = T extends HandleOr<infer U> ? U : never
type InnerParams<T extends unknown[]> = { [K in keyof T]: FlattenHandle<T[K]> }
type EvaluateFunc<T extends unknown[]> = (...params: InnerParams<T>) => unknown
declare function evaluate<Params extends unknown[], Func extends EvaluateFunc<Params> = EvaluateFunc<Params>>(
  pageFunction: Func | string, ...args: Params): Promise<Awaited<ReturnType<Func>>>
export async function f(total: number) {
  const r = await evaluate(([t]) => t + 1, [total])
  const s = await evaluate((t) => t + 1, total)
  return [r, s]
}
