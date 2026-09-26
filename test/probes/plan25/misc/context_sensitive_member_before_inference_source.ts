// @strict: true
// @tanstack/react-query useMutation({ onSuccess: (...args) => {...}, mutationFn }): the context-sensitive
// onSuccess comes first and must not fix TData/TVariables before mutationFn is inferred from
interface Options<TData, TVariables> {
    mutationFn?: (variables: TVariables) => Promise<TData>;
    onSuccess?: (data: TData, variables: TVariables) => unknown;
}
declare function useMutation<TData = unknown, TVariables = void>(options: Options<TData, TVariables>): [TData, TVariables];
declare const fn: (v: { id: string }) => Promise<number>;
const before = useMutation({ onSuccess: (data, variables) => {}, mutationFn: fn });
const after = useMutation({ mutationFn: fn, onSuccess: (data, variables) => {} });
