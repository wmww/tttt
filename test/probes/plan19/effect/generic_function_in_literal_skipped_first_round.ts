// @strict: true
// @target: es2022
interface Option<A> { _a: A }
interface HM<K, V> { _k: K; _v: V }
interface Chunk<A> { _c: A }
declare const match: {
  <B, A, C = B>(options: { readonly onNone: () => B; readonly onSome: (a: A) => C }): (self: Option<A>) => B | C
  <A, B, C = B>(self: Option<A>, options: { readonly onNone: () => B; readonly onSome: (a: A) => C }): B | C
}
declare const get: {
  <K1 extends K, K>(key: K1): <V>(self: HM<K, V>) => Option<V>
  <K1 extends K, K, V>(self: HM<K, V>, key: K1): Option<V>
}
declare const size: <A>(self: Chunk<A>) => number
declare const m: HM<string, Chunk<number>>
export const s = match(get(m, "k"), { onNone: () => 0, onSome: size })
