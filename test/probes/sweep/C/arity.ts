// @strict: true
declare function f(a: (e: Error) => void, b: number): void;
f((e) => {});
declare function g<T>(a: T, cb: (x: T) => void, b: number): void;
g(1, x => {});
declare function h(a: (e: Error) => void): void;
h((e) => {}, 1);
