type MaybeReadonly<T> = T | Readonly<T>;
interface Schema<TItems extends MaybeReadonly<string[]>> { items: TItems; kind: 'tuple' }
type I<T> = T extends Schema<infer X> ? X : 0;
type A2 = I<Schema<[]>>;
type M<T extends MaybeReadonly<string[]>> = {-readonly [K in keyof T]: T[K]};
type A3 = M<[]>;
type D<T> = T extends Schema<infer TItems extends string[]> ? { -readonly [K in keyof TItems]: TItems[K] } : undefined;
type A4 = D<Schema<[]>>;
type D5<T> = T extends Schema<infer TItems> ? { -readonly [K in keyof TItems]: TItems[K] } : undefined;
type A5 = D5<Schema<[]>>;
type R<T> = { readonly [K in keyof T]: T[K] };
type A6 = R<[]>;
type A7 = M<readonly []>;
type P<T> = { [K in keyof T]?: T[K] };
type A8 = P<[]>;
