// @strict: true
type Foo<T extends string> = { v: T };
type Bar<T> = T extends string ? Foo<T> : never;
type Args<T, K extends keyof T> = T[K] extends unknown[] ? (...args: T[K]) => void : never;
type Tup<T> = T extends [infer A] ? A : never;
type Spread<T> = [T] extends [any[]] ? [...T] : never;
type Pick2<T, K> = K extends keyof T ? T[K] : never;
type R1 = Bar<"a">;
type R2 = Args<{ a: [number] }, "a">;
function f<T, K extends keyof T>(t: T, k: K) {
    type L = T[K] extends string ? Lowercase<T[K]> : never;
}
