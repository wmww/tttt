//// [test/cases/ttttDeclarationImportedDistributiveAlias.ts] ////

//// [types.ts]
export type _<T> = T extends {} ? { [k in keyof T]: T[k] } : T;
export type Optionals<T> = Extract<T, null | undefined>;
export type Merge<T, U> = ({ [P in keyof T]: P extends keyof U ? U[P] : T[P] } & U) | Optionals<T>;
type Local<T> = T extends {} ? { [k in keyof T]: T[k] } : T;
export declare function set<A>(): A[];
export function local<TIn, U extends object>() {
    return set<Local<Merge<TIn, { u: U }>>>();
}
//// [main.ts]
import { _, Merge, set } from './types';
export function shape<TIn, U extends object>() {
    return set<_<Merge<TIn, { u: U }>>>();
}
export function concrete() {
    return set<_<Merge<{ a: 1 } | null, { u: 2 }>>>();
}


//// [types.js]
export function local() {
    return set();
}
//// [main.js]
import { set } from './types';
export function shape() {
    return set();
}
export function concrete() {
    return set();
}


//// [types.d.ts]
export type _<T> = T extends {} ? {
    [k in keyof T]: T[k];
} : T;
export type Optionals<T> = Extract<T, null | undefined>;
export type Merge<T, U> = ({
    [P in keyof T]: P extends keyof U ? U[P] : T[P];
} & U) | Optionals<T>;
type Local<T> = T extends {} ? {
    [k in keyof T]: T[k];
} : T;
export declare function set<A>(): A[];
export declare function local<TIn, U extends object>(): (Local<Extract<TIn, null | undefined>> | Local<{ [P in keyof TIn]: P extends "u" ? {
    u: U;
}[P] : TIn[P]; } & {
    u: U;
}>)[];
export {};
//// [main.d.ts]
import { _, Merge } from './types';
export declare function shape<TIn, U extends object>(): _<Merge<TIn, {
    u: U;
}>>[];
export declare function concrete(): _<Merge<{
    a: 1;
} | null, {
    u: 2;
}>>[];
