type Om<O> = { [K in keyof O as {} extends Record<K, unknown> ? never : K]: O[K]; };
type Pk<O> = { [K in keyof O as {} extends Record<K, unknown> ? K : never]: O[K]; };
type Simplify<T> = {[K in keyof T]: T[K]};
type SM<D, S> = { [K in keyof D as K extends keyof S ? never : K]: D[K]; } & S;
type D = {[x: string]: string | undefined; a: string};
type S = {[x: string]: string | undefined; b?: string};
declare const a: Simplify<{} & {a: string}>;
const r: {a: string} = a;
