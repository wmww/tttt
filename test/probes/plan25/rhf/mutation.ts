// react-query useMutation: TVariables must come from mutationFn, inferred in the first pass from the
// non-context-sensitive members of an object literal that also has a context-sensitive onSuccess
interface Options<TData = unknown, TVariables = void> {
  mutationFn?: (variables: TVariables) => Promise<TData>;
  onSuccess?: (data: TData, variables: TVariables) => unknown;
}
declare function useMutation<TData = unknown, TVariables = void>(options: Options<TData, TVariables>): TVariables;
declare const deleteUser: ({ userId }: { userId: string }) => Promise<number>;

const r = useMutation({
  onSuccess: (...args) => {},
  mutationFn: deleteUser,
});

// Corpus shape: a spread in between, the error lands on mutationFn instead
declare const restConfig: { onError?: (e: unknown) => void };
const r2 = useMutation({
  onSuccess: (...args) => {},
  ...restConfig,
  mutationFn: deleteUser,
});
