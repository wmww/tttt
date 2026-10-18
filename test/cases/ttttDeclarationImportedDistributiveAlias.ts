// @declaration: true
// A distributive conditional type referenced through an import and distributed over a union is the union by the
// alias written (tsc's getTypeFromTypeAliasReference passes the import's alias down, getConditionalTypeInstantiation's
// mapTypeWithAlias): `_<Merge<T, U>>`, where a local reference is the plain union of each member's
// @filename: types.ts
export type _<T> = T extends {} ? { [k in keyof T]: T[k] } : T;
export type Optionals<T> = Extract<T, null | undefined>;
export type Merge<T, U> = ({ [P in keyof T]: P extends keyof U ? U[P] : T[P] } & U) | Optionals<T>;
type Local<T> = T extends {} ? { [k in keyof T]: T[k] } : T;
export declare function set<A>(): A[];
export function local<TIn, U extends object>() {
    return set<Local<Merge<TIn, { u: U }>>>();
}
// @filename: main.ts
import { _, Merge, set } from './types';
export function shape<TIn, U extends object>() {
    return set<_<Merge<TIn, { u: U }>>>();
}
export function concrete() {
    return set<_<Merge<{ a: 1 } | null, { u: 2 }>>>();
}
