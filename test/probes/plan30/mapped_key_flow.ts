type Tuple<T> = readonly [T, ...T[]];
type A<TTarget, TTypes extends Tuple<keyof TTarget>> = { [K in keyof TTypes]: TTarget[TTypes[K]] };
type B<TTypes extends string[]> = { [K in keyof TTypes]: TTypes[K] extends string ? 1 : 0 };
type C<TTypes extends string[]> = { [K in keyof TTypes]: Lowercase<TTypes[K]> };
