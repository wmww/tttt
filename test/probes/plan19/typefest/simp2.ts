// @strict: true
type Simplify<T> = {[K in keyof T]: T[K]};
type D = {[x: string]: string; a: "a"};
type Om<O> = { [K in keyof O as string extends K ? never : K]: O[K]; };
type Pk<O> = { [K in keyof O as string extends K ? K : never]: O[K]; };
type Id<O> = { [K in keyof O as K]: O[K]; };
declare const a: Simplify<Pk<D> & Om<D>>;
declare const b: Simplify<{[x: string]: string} & Om<D>>;
declare const c: Simplify<Pk<D> & {a: "a"}>;
declare const d: Simplify<Id<{a: "a"}> & {[x: string]: string}>;
declare const e: Simplify<Id<{a: "a"}>>;
