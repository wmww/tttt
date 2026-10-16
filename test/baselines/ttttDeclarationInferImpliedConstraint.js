//// [test/cases/ttttDeclarationInferImpliedConstraint.ts] ////

//// [a.ts]
export type Config = { page: string; meta: number };
type Src<C extends Config = Config> = { c: C } | { d: C };
type Gen<T> = T extends Src<infer D> ? D['meta'] : never;
type Gen2<T> = T extends Record<infer K extends string, Src> ? { [k in K]: T[k] extends Src<infer D> ? D['page'] : never }[K] : never;
type T3<T> = T extends `a${infer S extends string}` ? S : T extends [...infer R extends unknown[]] ? R : T extends `b${infer N extends number}` ? N : never;
type Box<X extends string> = { x: X };
type T4<T> = T extends Box<infer S extends 'a' | 'b'> ? S : T extends Box<infer Q extends string> ? Q : never;
export declare function f<I>(i: I): { a: Gen<I>; b: Gen2<I>; c: T3<I>; d: T4<I> };
//// [b.ts]
import { f } from './a';
export function g<J>(j: J) { return f(j); }


//// [a.js]
export {};
//// [b.js]
import { f } from './a';
export function g(j) { return f(j); }


//// [a.d.ts]
export type Config = {
    page: string;
    meta: number;
};
type Src<C extends Config = Config> = {
    c: C;
} | {
    d: C;
};
type Gen<T> = T extends Src<infer D> ? D['meta'] : never;
type Gen2<T> = T extends Record<infer K extends string, Src> ? {
    [k in K]: T[k] extends Src<infer D> ? D['page'] : never;
}[K] : never;
type T3<T> = T extends `a${infer S extends string}` ? S : T extends [...infer R extends unknown[]] ? R : T extends `b${infer N extends number}` ? N : never;
type Box<X extends string> = {
    x: X;
};
type T4<T> = T extends Box<infer S extends 'a' | 'b'> ? S : T extends Box<infer Q extends string> ? Q : never;
export declare function f<I>(i: I): {
    a: Gen<I>;
    b: Gen2<I>;
    c: T3<I>;
    d: T4<I>;
};
export {};
//// [b.d.ts]
export declare function g<J>(j: J): {
    a: J extends {
        c: infer D extends import("./a").Config;
    } | {
        d: infer D extends import("./a").Config;
    } ? D["meta"] : never;
    b: J extends Record<infer K extends string, {
        c: import("./a").Config;
    } | {
        d: import("./a").Config;
    }> ? { [k in K]: J[k] extends {
        c: infer D_1 extends import("./a").Config;
    } | {
        d: infer D_1 extends import("./a").Config;
    } ? D_1["page"] : never; }[K] : never;
    c: J extends `a${infer S}` ? S : J extends [...infer R] ? R : J extends `b${infer N extends number}` ? N : never;
    d: J extends {
        x: infer S_1 extends "a" | "b";
    } ? S_1 : J extends {
        x: infer Q extends string;
    } ? Q : never;
};
