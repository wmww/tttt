// @strict: true
// A never index reads the index signature that never is assignable to (tsc's getPropertyTypeForIndexType with
// getApplicableIndexInfo): `{ [id: number]: P }[never]` is P, like `O[Extract<keyof O, string>]` over a number-indexed
// O (amcharts5's $object.entries)
type R = { [id: number]: string };
type S = { [k: string]: boolean; a: number };
type P = { a: number };
declare const a1: R[never];
declare const a2: S[never];
declare const a3: P[never];
type Keyof<T> = Extract<keyof T, string>;
declare function entries<O>(object: O): Array<[Keyof<O>, O[Keyof<O>]]>;
declare const parents: { [id: number]: P };
export const firstUid = entries(parents)[0][1].a;
export const all = [a1, a2, a3];
