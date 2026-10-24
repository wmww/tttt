// @strict: true
// An annotated destructuring's initializer is looked at only for whether it can be undefined, through tsc's
// getTypeOfInitializer (getTypeOfExpression): a call of one non-generic signature is its return type there, its
// arguments unchecked, so a callback that reads a destructured name isn't circular
type R = { use: () => number; other: string };
declare function make(fn: () => number): R;
export const { use, other }: R = make(() => use() + 1);
declare function makeGeneric<T>(fn: () => T): R;
export const { use: use2 }: R = makeGeneric(() => use2() + 1);
