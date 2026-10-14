// @strict: true
// A generic alias referenced through an import passes its own name down to the instantiation of its body (tsgo's
// getTypeFromTypeAliasReference, instantiateTypeWorker): a union or intersection body that is another alias's
// instantiation (here through a conditional wrapper that resolves to its argument, like ajv's JSONSchemaType) becomes
// a union by the import's alias, `J<number>`, another type than `U<number, false>`.
// (mermaid's create-types-from-json-schema.mts related `JSONSchemaType<MermaidConfig>` as `UncheckedJSONSchemaType<..>`,
// whose own structure was then on the maybe stack, and found a spurious TS2345 depending on the type ids before it)
// @filename: lib.ts
type W<N extends string, Type> = undefined extends null ? `needs ${N}` : Type;
type U<T, P extends boolean> = { a: T[] } | { b: P };
type I<T> = ({ a: T } | { b: T[] }) & { k?: string };
type O<T> = { o: T };
export type J<T> = W<"J", U<T, false>>;
export type K<T> = I<T>;
export type L<T> = O<T>;
export type M<T> = Array<T>;
// @filename: main.ts
import type { J, K, L, M } from "./lib";
declare const j: J<number>;
declare const k: K<number>;
declare const l: L<number>;
declare const m: M<number>;
const j1: { c: 1 } = j;
const k1: { c: 1 } = k;
const l1: { c: 1 } = l;
const m1: { c: 1 } = m;
