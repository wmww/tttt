// @strict: true
// A NoInfer target left over once a union's matched members come off (`D | undefined` to `NoInfer<T> | undefined`)
// infers nothing: T keeps its default. From @tanstack/query-core's setQueryData (lklynet/aurral)
interface D { a: number }
declare const updater: (current: D | undefined) => D | undefined;

declare function f1<T = unknown>(u: NoInfer<T> | undefined | ((i: unknown) => NoInfer<T> | undefined)): T;
const r1 = f1(updater);
declare function f3<T = unknown>(u: (i: unknown) => NoInfer<T> | undefined): T;
const r3 = f3(updater);
declare function f4<T = string>(u: NoInfer<T> | null | undefined, v: T | undefined): T;
const r4 = f4(1 as number | undefined, undefined);

type Updater<TInput, TOutput> = TOutput | ((input: TInput) => TOutput);
declare const tag: unique symbol;
type DataTag<TType, TValue> = TType & { [tag]: TValue };
type InferDataFromTag<TQueryFnData, TTaggedQueryKey extends readonly unknown[]> =
    TTaggedQueryKey extends DataTag<unknown, infer TaggedValue> ? TaggedValue : TQueryFnData;
declare function setQueryData<TQueryFnData = unknown, TTaggedQueryKey extends readonly unknown[] = readonly unknown[],
    TInferredQueryFnData = InferDataFromTag<TQueryFnData, TTaggedQueryKey>>(
    queryKey: TTaggedQueryKey,
    updater: Updater<InferDataFromTag<NoInfer<TQueryFnData>, TTaggedQueryKey> | undefined, NoInfer<TInferredQueryFnData> | undefined>,
): NoInfer<TInferredQueryFnData> | undefined;
declare const key: readonly unknown[];
const r = setQueryData(key, updater);
