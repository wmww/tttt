// An `infer X` of a conditional type printed as a type (here expanded: its aliases aren't accessible from b.ts) prints
// its constraint, written or implied by a type argument's position (`Src<infer D>` gives D Src's `C extends Config`),
// unless the position implies the same constraint, `${infer S extends string}` printing as `${infer S}`: tsc's
// typeToTypeNodeHelper compares with getInferredTypeParameterConstraint(omitTypeReferences) (fumadocs' SourceUnion
// in ui's and discord.js' source.d.ts lost `infer D extends {...}`)
// @strict: true
// @declaration: true
// @filename: /a.ts
export type Config = { page: string; meta: number };
type Src<C extends Config = Config> = { c: C } | { d: C };
type Gen<T> = T extends Src<infer D> ? D['meta'] : never;
type Gen2<T> = T extends Record<infer K extends string, Src> ? { [k in K]: T[k] extends Src<infer D> ? D['page'] : never }[K] : never;
type T3<T> = T extends `a${infer S extends string}` ? S : T extends [...infer R extends unknown[]] ? R : T extends `b${infer N extends number}` ? N : never;
type Box<X extends string> = { x: X };
type T4<T> = T extends Box<infer S extends 'a' | 'b'> ? S : T extends Box<infer Q extends string> ? Q : never;
export declare function f<I>(i: I): { a: Gen<I>; b: Gen2<I>; c: T3<I>; d: T4<I> };
// @filename: /b.ts
import { f } from './a';
export function g<J>(j: J) { return f(j); }
