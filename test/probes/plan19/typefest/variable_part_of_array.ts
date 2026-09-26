// @strict: true
type T = [...Array<{a: 1}>, {b: 2}];
type S<T extends readonly unknown[], R extends readonly unknown[] = []> =
    number extends T['length'] ? T extends readonly [infer U, ...infer V] ? S<V, [...R, U]> : R : T;
type V<T extends readonly unknown[]> = T extends readonly [...S<T>, ...infer U] ? U : [];
type A = S<T>;
type B = V<T>;
type C = T extends readonly [...[], ...infer U] ? U : 0;
type D = [string, ...number[]] extends readonly [...[string], ...infer U] ? U : 0;
