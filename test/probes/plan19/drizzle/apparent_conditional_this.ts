// @strict: true
// A member of a deferred conditional type whose constraint is a class sees the conditional type as its `this` (tsc's
// getApparentType + getTypeWithThisArgument): chained `this`-returning builder methods on `T` stay generic in T
declare class QB<S> {
    _: { sel: S };
    join<N extends string>(name: N): Join<this, N>;
}
type Join<T extends QB<any>, N extends string> = T extends any ? QB<T["_"]["sel"] & Record<N, 1>> : never;
function twice<T extends QB<{}>>(qb: T) {
    return qb.join("a").join("b");
}
declare const q: QB<{ x: 1 }>;
const r = twice(q);
const sel = r._.sel;
