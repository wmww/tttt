// @strict: true
// nodeFactory.ts: a generic function passed to a generic function returning a function type keeps its type
// parameters on the result (higher-order inference), so explicit type arguments on the result are legal
interface Node { kind: number; }
interface A extends Node { kind: 1; a: string; }
interface B extends Node { kind: 2; b: string; }
declare function memoizeOne<K extends string | number | boolean | undefined, R>(callback: (arg: K) => R): (arg: K) => R;
declare function create<T extends Node>(kind: T["kind"]): T;
const getCreate = memoizeOne(<T extends Node>(kind: T["kind"]) => () => create<T>(kind));
const a: A = getCreate<A>(1)();
const b = getCreate<B>(2);
const factory: { createA(): A } = { createA: () => getCreate<A>(1)() };
