// `x!` of a type parameter is NonNullable<T> (T & {}, tsc's getNonNullableType through getAdjustedTypeWithFacts), which is
// still assignable to T; it was the stripped constraint (xstate observable.ts: input: state.input!)
// @strict: true
type NonReducibleUnknown = {} | null | undefined;
function f<TInput extends NonReducibleUnknown>(s: { input: TInput }) {
  const x: TInput = s.input!;
  const y = s.input!;
}
function g<TInput>(s: { input: TInput }) {
  const x: TInput = s.input!;
}
