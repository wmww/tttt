// @strict: true
// @lib: es2016,dom
declare const RefSymbol: unique symbol
declare const ShallowRefMarker: unique symbol
export declare const RawSymbol: unique symbol
interface Ref<T = any, S = T> { get value(): T; set value(_: S); [RefSymbol]: true }
type ShallowRef<T = any, S = T> = Ref<T, S> & { [ShallowRefMarker]?: true }
interface RefUnwrapBailTypes { runtimeDOMBailTypes: Node | Window }
// `keyof`/indexed access types or requiring the property for plain assignment.
declare class ShallowReactiveBrandClass {
  private __shallowReactiveBrand?: never
}

export type ShallowReactiveBrand = ShallowReactiveBrandClass
type Primitive = string | number | boolean | bigint | symbol | undefined | null
export type Builtin = Primitive | Function | Date | Error | RegExp
export type DeepReadonly<T> = T extends Builtin
  ? T
  : T extends Map<infer K, infer V>
    ? ReadonlyMap<DeepReadonly<K>, DeepReadonly<V>>
    : T extends ReadonlyMap<infer K, infer V>
      ? ReadonlyMap<DeepReadonly<K>, DeepReadonly<V>>
      : T extends WeakMap<infer K, infer V>
        ? WeakMap<DeepReadonly<K>, DeepReadonly<V>>
        : T extends Set<infer U>
          ? ReadonlySet<DeepReadonly<U>>
          : T extends ReadonlySet<infer U>
            ? ReadonlySet<DeepReadonly<U>>
            : T extends WeakSet<infer U>
              ? WeakSet<DeepReadonly<U>>
              : T extends Promise<infer U>
                ? Promise<DeepReadonly<U>>
                : T extends Ref<infer U, unknown>
                  ? Readonly<Ref<DeepReadonly<U>>>
                  : T extends {}
                    ? { readonly [K in keyof T]: DeepReadonly<T[K]> }
                    : Readonly<T>
export type UnwrapRef<T> =
  T extends ShallowRef<infer V, unknown>
    ? V
    : T extends Ref<infer V, unknown>
      ? UnwrapRefSimple<V>
      : UnwrapRefSimple<T>

export type UnwrapRefSimple<T> = T extends
  | Builtin
  | Ref
  | RefUnwrapBailTypes[keyof RefUnwrapBailTypes]
  | { [RawSymbol]?: true }
  ? T
  : T extends ShallowReactiveBrand
    ? T
    : T extends Map<infer K, infer V>
      ? Map<K, UnwrapRefSimple<V>> & UnwrapRef<Omit<T, keyof Map<any, any>>>
      : T extends WeakMap<infer K, infer V>
        ? WeakMap<K, UnwrapRefSimple<V>> &
            UnwrapRef<Omit<T, keyof WeakMap<any, any>>>
        : T extends Set<infer V>
          ? Set<UnwrapRefSimple<V>> & UnwrapRef<Omit<T, keyof Set<any>>>
          : T extends WeakSet<infer V>
            ? WeakSet<UnwrapRefSimple<V>> &
                UnwrapRef<Omit<T, keyof WeakSet<any>>>
            : T extends ReadonlyArray<any>
              ? { [K in keyof T]: UnwrapRefSimple<T[K]> }
              : T extends object
                ? {
                    [P in keyof T]: P extends symbol ? T[P] : UnwrapRef<T[P]>
                  }
                : T
export type UnwrapNestedRefs<T> = T extends Ref ? T : UnwrapRefSimple<T>

declare const ReactiveMarkerSymbol: unique symbol

export interface ReactiveMarker {
  [ReactiveMarkerSymbol]?: void
}

export type Reactive<T> = UnwrapNestedRefs<T> &
  (T extends readonly any[] ? ReactiveMarker : {})
declare function reactive<T extends object>(target: T): Reactive<T>
declare function readonly<T extends object>(target: T): DeepReadonly<UnwrapNestedRefs<T>>
declare const isObject: (val: unknown) => val is Record<any, any>
export const toReactive = <T extends unknown>(value: T): T =>
  isObject(value) ? reactive(value) : value
export const toReadonly = <T extends unknown>(value: T): DeepReadonly<T> =>
  isObject(value) ? readonly(value) : (value as DeepReadonly<T>)
