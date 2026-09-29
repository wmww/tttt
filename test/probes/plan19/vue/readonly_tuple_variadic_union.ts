// @strict: true
interface Ref<T> { value: T }
interface ReactiveMarker { __marker?: void }
type WatchSource<T = any> = Ref<T> | (() => T)
type MultiWatchSources = (WatchSource<unknown> | object)[]
type MaybeUndefined<T, I> = I extends true ? T | undefined : T
type MapSources<T, Immediate> = {
  [K in keyof T]: T[K] extends WatchSource<infer V> ? MaybeUndefined<V, Immediate> : T[K] extends object ? MaybeUndefined<T[K], Immediate> : never
}
type WatchCallback<V = any, OV = any> = (value: V, oldValue: OV) => any
declare function watch<T extends Readonly<MultiWatchSources>, Immediate extends Readonly<boolean> = false>(
  sources: readonly [...T] | T,
  cb: [T] extends [ReactiveMarker] ? WatchCallback<T, MaybeUndefined<T, Immediate>> : WatchCallback<MapSources<T, false>, MapSources<T, Immediate>>,
): void
declare function expectType<T>(value: T): void
type Foo = readonly [Ref<string>, () => number]
declare const readonlyArr: Foo
watch(readonlyArr, (values) => {
  expectType<Readonly<[string, number]>>(values)
})
