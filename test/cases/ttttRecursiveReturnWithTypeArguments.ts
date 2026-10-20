// A bare call to the function itself doesn't contribute to its inferred return type, with type arguments too (tsc's
// checkAndAggregateReturnExpressionTypes looks at the call's expression, the identifier): `return reduce<T>(t)` isn't
// TS7023 (mutation of declarationsWithRecursiveInternalTypesProduceUniqueTypeParams). Through an exported variable it
// does contribute: the name resolves to the export symbol, which isn't a constant reference (isConstantVariable)
// @strict: true
export const f = <T>(t: T) => {
    const reduce = <U>(u: U) => {
        if (Math.random()) return { u };
        return reduce<T>(t);
    };
    return reduce;
};
const k = <U>(u: U) => {
    if (Math.random()) return [u];
    return k<U>(u);
};
const k2 = (u: number) => {
    if (Math.random()) return [u];
    return k2(u);
};
export { k, k2 };
export const g = <U>(u: U) => {
    if (Math.random()) return [u];
    return g<U>(u);
};
export const h = (u: number) => {
    if (Math.random()) return [u];
    return h(u);
};
export const h2 = function (u: number) {
    if (Math.random()) return [u];
    return h2(u);
};
export namespace N {
    export const n = (u: number) => {
        if (Math.random()) return [u];
        return n(u);
    };
}
