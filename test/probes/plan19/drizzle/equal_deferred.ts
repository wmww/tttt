type Equal<X, Y> = (<T>() => T extends X ? 1 : 2) extends (<T>() => T extends Y ? 1 : 2) ? true : false;
type IV<S> = Equal<S, { [x: string]: unknown }> extends true ? { a: 1 } : { [K in keyof S]: S[K] };
type E<S> = Equal<S, { [x: string]: unknown }>;
function f<T extends Record<string, unknown>>(iv: IV<T>, e: E<T>, e2: E<{ [K in keyof T]: 1 }>) {
    iv;
    e;
    e2;
}
