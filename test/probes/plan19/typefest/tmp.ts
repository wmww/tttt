// @strict: true
type H<T> = T[keyof T];
type P<U extends {x: string}, W extends number[]> = H<{a: W[0]}>;
