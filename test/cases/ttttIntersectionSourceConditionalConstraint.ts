// @strict: true
// An intersection source's conditional member relates its constraints without IntersectionState.Source (tsc's
// isRelatedTo): its `{}` still has an inferable index signature (mikro-orm's QueryBuilder.having)
type IsNever<T> = [T] extends [never] ? true : false;
type Keys<R extends string> = { [K in R]?: string };
type Cond<R extends string> = IsNever<R> extends true ? {} : Keys<R>;
type Filter<T> = { -readonly [K in keyof T]?: T[K] | null };
type Query<T, R extends string = never> = Filter<T> & Cond<R>;

declare const key: symbol;
function having<T, R extends string>(cond: Query<T, R> | string) {
    if (typeof cond === "string") {
        cond = { [key]: [] } as Query<T, R>;
    }
    return cond;
}
