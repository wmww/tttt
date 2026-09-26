// @strict: true
type Om<O> = { [K in keyof O as {} extends Record<K, unknown> ? never : K]: O[K]; };
type Pk<O> = { [K in keyof O as {} extends Record<K, unknown> ? K : never]: O[K]; };
type Simplify<T> = {[K in keyof T]: T[K]};
type D = {[x: string]: string; a: "a"};
declare const a: Simplify<Pk<D> & Om<D>>;
declare const b: Om<D>;
declare const c: Pk<D>;
declare const d: keyof Pk<D>;
declare const e: keyof (Pk<D> & Om<D>);
