// @strict: true
type Covariant<A> = (_: never) => A
interface Micro<out A, out E = never, out R = never> { readonly _V: { _A: Covariant<A>, _E: Covariant<E>, _R: Covariant<R> } }
interface Proto<Tag extends string, E> { readonly _C: { _E: Covariant<E> }; readonly _tag: Tag }
interface Die extends Proto<"Die", never> { readonly defect: unknown }
interface Fail<E> extends Proto<"Fail", E> { readonly error: E }
type MicroCause<E> = Die | Fail<E>
declare const failCause: <E>(cause: MicroCause<E>) => Micro<never, E>
declare const andThen: {
  <A, E, R, X>(self: Micro<A, E, R>, f: (a: A) => X): [X] extends [Micro<infer A1, infer E1, infer R1>] ? Micro<A1, E | E1, R | R1> : Micro<X, E, R>
}
export const t2 = <B, E, E2, R2, EB extends MicroCause<E>>(cause: EB, f: (a: EB) => Micro<B, E2, R2>) => andThen(f(cause), failCause(cause))
export const t3 = <E, EB extends MicroCause<E>>(cause: EB) => failCause(cause)
