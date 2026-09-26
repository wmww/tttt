// Intra-expression inference (TS 4.7): context-sensitive members are checked in order, each one's
// inferences visible to the next (useInfiniteQuery's queryFn -> getNextPageParam)
declare function q<T>(o: { queryFn: (ctx: { p: number }) => T; next: (last: T) => number }): T;
const r = q({ queryFn: ({ p }) => ({ page: p }), next: (last) => last.page + 1 });
