// @strict: true
type A<T, K> = T[K];
type B<T, K extends keyof T> = T[K];
type C<T extends object, K extends string> = T[K];
type D<T> = T extends object ? T["a"] : never;
type E<T extends { a: string }> = T["a"];
type F<T> = T[keyof T];
type G<T, K extends string> = { [P in K]: T }[K];
type H<T, K> = K extends keyof T ? T[K] : never;
