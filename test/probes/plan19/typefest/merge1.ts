// @strict: true
type OmitIndexSignature<O> = { [K in keyof O as {} extends Record<K, unknown> ? never : K]: O[K]; };
type PickIndexSignature<O> = { [K in keyof O as {} extends Record<K, unknown> ? K : never]: O[K]; };
type Simplify<T> = {[K in keyof T]: T[K]} & {};
type SimpleMerge<D, S> = { [K in keyof D as K extends keyof S ? never : K]: D[K]; } & S;
type Merge<D, S> = Simplify<SimpleMerge<PickIndexSignature<D>, PickIndexSignature<S>> & SimpleMerge<OmitIndexSignature<D>, OmitIndexSignature<S>>>;
type D = {[x: string]: string | undefined; a: string};
type S = {[x: string]: string | undefined; b?: string};
declare const m: Merge<D, S>;
declare const o1: OmitIndexSignature<D>;
declare const o2: SimpleMerge<OmitIndexSignature<D>, OmitIndexSignature<S>>;
declare const p2: SimpleMerge<PickIndexSignature<D>, PickIndexSignature<S>>;
declare const s1: Simplify<typeof p2 & typeof o2>;
