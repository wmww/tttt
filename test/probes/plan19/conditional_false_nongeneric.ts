const o = { a: () => 1 };
type T1 = typeof o extends { a: string } ? 1 : 0;
type T2 = { a: () => 1 } extends { a: () => 2 } ? 1 : 0;
declare function f(x: number): string;
type T3 = typeof f extends (x: string) => string ? 1 : 0;
declare const tag: unique symbol;
type TC<T> = { readonly [tag]: T };
type T4 = TC<1> extends TC<2> ? 1 : 0;
