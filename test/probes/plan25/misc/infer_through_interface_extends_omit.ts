// @strict: true
type MutationFunction<TData, TVariables> = (variables: TVariables) => Promise<TData>;
interface MutationOptions<TData = unknown, TVariables = void> {
    mutationFn?: MutationFunction<TData, TVariables>;
    onSuccess?: (data: TData, variables: TVariables) => unknown;
    _defaulted?: boolean;
}
type OmitKeyof<TObject, TKey extends TStrictly extends 'safely' ? keyof TObject | (string & Record<never, never>) : keyof TObject, TStrictly extends 'strictly' | 'safely' = 'strictly'> = Omit<TObject, TKey>;
interface UseMutationOptions<TData = unknown, TVariables = void> extends OmitKeyof<MutationOptions<TData, TVariables>, '_defaulted'> {}
declare function useMutation<TData = unknown, TVariables = void>(options: UseMutationOptions<TData, TVariables>): [TData, TVariables];

declare const fn: (v: { id: string }) => Promise<number>;
const r = useMutation({ mutationFn: fn });
const check: [number, { id: string }] = r;
const r2 = useMutation({ onSuccess: (...args) => {}, mutationFn: fn });
const check2: [number, { id: string }] = r2;
