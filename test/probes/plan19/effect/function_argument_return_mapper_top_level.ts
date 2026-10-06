// A function argument's contextual type is instantiated with the call's return mapper only at its top level (tsc's
// instantiateInstantiableTypes): `new Promise` infers nothing from zipRight's A2, it is only in its return mapper (effect query.test 51)
// @target: es2015
// @lib: es2015,dom
// @strict: true
interface Effect<out A, out E = never, out R = never> { readonly _A: () => A; readonly _E: () => E; readonly _R: () => R }
declare const promise: <A>(evaluate: (signal: AbortSignal) => PromiseLike<A>) => Effect<A>
declare const zipRight: {
  <A2, E2, R2>(that: Effect<A2, E2, R2>, options?: { readonly concurrent?: boolean | undefined }): <A, E, R>(self: Effect<A, E, R>) => Effect<A2, E2 | E, R2 | R>
  <A, E, R, A2, E2, R2>(self: Effect<A, E, R>, that: Effect<A2, E2, R2>, options?: { readonly concurrent?: boolean | undefined }): Effect<A2, E2 | E, R2 | R>
}
export const delay = <A, E, R>(self: Effect<A, E, R>) =>
  zipRight(promise(() => new Promise((r) => setTimeout(() => r(0), 0))), self)
