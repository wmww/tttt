// @declaration: true
// @strict: true
// A homomorphic mapped type over a tuple maps a rest element as an array (tsc's instantiateMappedTupleType), which an
// error element makes the error type (instantiateMappedArrayType), whose element type is none: the element is unknown
import type { Missing } from "nowhere";
type Writeable<T> = { -readonly [P in keyof T]: T[P] };
type Partial2<T> = { [P in keyof T]?: T[P] };
export declare function f<T extends readonly unknown[]>(x: T): Writeable<T>;
export declare function g<T extends readonly unknown[]>(x: T): Partial2<T>;
export const d = f([] as any as [Missing, ...Missing[]]);
export const e = f([] as any as [number, ...Missing[], string]);
export const h = g([] as any as [Missing, ...Missing[]]);
