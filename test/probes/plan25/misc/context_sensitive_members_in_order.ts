// @strict: true
// react-query infiniteQueryOptions({ queryFn: ({ pageParam }) => fetch(..), getNextPageParam: (lastPage) => .. }):
// both members are context-sensitive; checking queryFn (2nd pass, in order) infers TData before
// getNextPageParam's parameter is fixed from it
declare function opts<TData, TPage>(o: {
    queryFn: (ctx: { pageParam: TPage }) => Promise<TData>;
    getNextPageParam: (lastPage: TData) => TPage | undefined;
    initialPageParam: TPage;
}): TData;
declare function get(p: number): Promise<{ meta: { page: number } }>;
const r = opts({
    queryFn: ({ pageParam }) => get(pageParam),
    getNextPageParam: (lastPage) => lastPage.meta.page + 1,
    initialPageParam: 1,
});
