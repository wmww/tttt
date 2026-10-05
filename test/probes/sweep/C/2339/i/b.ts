declare function f<K extends never>(k: K): [unknown[K], unknown[K] extends any[] ? 1 : 2, (unknown[K] extends never ? true : false)];
declare function g<T, K extends keyof T>(t: T, k: K): T[K] extends any[] ? 1 : 2;
declare const u: unknown;
const x = g(u, "a" as never);
