// @strict: true
declare function reduceLeft<T, U>(array: readonly T[] | undefined, f: (memo: U, value: T, i: number) => U, initial: U): U;
declare function reduceLeft<T>(array: readonly T[], f: (memo: T, value: T, i: number) => T): T | undefined;
interface Sig { a: number }
declare const sigs: readonly Sig[];
declare function combine(a: Sig, b: Sig): Sig;
export const r = reduceLeft(sigs, (left: Sig | undefined, right) => left === right || !left ? left : combine(left, right));
declare function g<T>(f: (a: T, b: T) => T): T;
export const r2 = g((a: string | undefined, b) => b);
