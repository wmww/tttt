// origin: npm @xneog/dsh-anonymous-user-id@0.1.0 https://github.com/gomes007-alt/xneog-harness packages/identity/anonymous-user-id/tsconfig.bench.json seed 20261102
// flip: no
// @strict: true
type Dict<T = any, K extends string | symbol = string> = { [key in K]: T }
interface Schema<S = any, T = S> { (data?: S | null): T; new (data?: S | null): T; uid: number }
type Constructor<T = any> = new (...args: any[]) => T
type From<X> =
  | X extends string | number | boolean ? Schema<X>
  : X extends Schema ? X
  : X extends typeof String ? Schema<string>
  : X extends Constructor<infer S> ? Schema<S>
  : never
type TypeS1<X> = X extends Schema<infer S, unknown> ? S : never
type TypeS<X> = TypeS1<From<X>>
type TypeT<X> = ReturnType<From<X>>
interface I {
  dict<X, Y extends Schema<any, string> = Schema<string>>(inner: X, sKey?: Y): Schema<Dict<TypeS<X>, TypeS<Y>>, Dict<TypeT<X>, TypeT<Y>>>
}
