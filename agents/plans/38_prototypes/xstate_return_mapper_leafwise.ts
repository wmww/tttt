// @target: es2020
// xstate types.test.ts 2006/2022/2038 (3 x TS2345 "'number' is not assignable to parameter of type
// 'TOutput | PromiseLike<TOutput>'"): fromPromise(() => new Promise((resolve) => resolve(1))).
// tsc: resolve is `(value: unknown) => void`, x is unknown, no error. The inner `new Promise` has the contextual type
// PromiseLike<TOutput> (outer TOutput, no inferences yet), so its returnMapper maps Promise's T to the outer TOutput.
// tsc applies a returnMapper only through instantiateInstantiableTypes (top-level type variables), so the callback's
// contextual type `(resolve: (value: T | PromiseLike<T>) => void, ...) => void` is untouched and T is fixed to unknown.
// tttt's second pass instantiates the whole parameter type with the return mapper (context_mapper = return_mapper
// when nothing has candidates), so `resolve` expects the outer TOutput.
declare function fromPromise<TOutput, TInput = unknown>(
  creator: (a: { input: TInput }) => PromiseLike<TOutput>): TOutput;
const x = fromPromise(() => new Promise((resolve) => resolve(1)));
