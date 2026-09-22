// @strict: true
interface NodeArray<T> extends ReadonlyArray<T> { pos: number }
interface I { kind: "i"; a: string; parent: string }
interface E { kind: "e"; a: string; parent: number }
declare function some<T>(array: readonly T[] | undefined): array is readonly T[];
declare function some<T>(array: readonly T[] | undefined, predicate: (value: T) => boolean): boolean;
declare function pred(e: I | E): boolean;
declare const elements: NodeArray<I> | NodeArray<E>;
const r = some(elements, pred);
declare function one<T>(array: readonly T[] | undefined): T;
const r2 = one(elements);
