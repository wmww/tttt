// tsc: r2 : Promise<[string, number]> (later interface declaration's overload group is tried first); tttt picks the
// Iterable overload from the first declaration (Promise<...[]>). tsc's reorderCandidates puts each new declaration
// parent group of the same symbol at the front. This is Promise.all in lib es2015.iterable vs es2015.promise.
// @target: es2020
interface PC {
  all<T>(values: Iterable<T | PromiseLike<T>>): Promise<Awaited<T>[]>;
}
interface PC {
  all<T extends readonly unknown[] | []>(values: T): Promise<{ -readonly [P in keyof T]: Awaited<T[P]>; }>;
}
declare const p: PC;
const r2 = p.all(['x', 1]);
declare const u: unknown;
const r3 = Promise.all(['x', u]);
