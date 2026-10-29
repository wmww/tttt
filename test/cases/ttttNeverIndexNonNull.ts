// @strict: true
// An element access by never still says its object is possibly undefined (tsc's checkElementAccessExpression starts
// with checkNonNullExpression, whatever the index), and the access is the non-nullable object's: `t[null!]` reads the
// string index signature, any. Seed 20261113, controlFlowGenericTypes drop-as: `iSpec[null!`
function f<T extends Record<string, any> | undefined>(t: T, x: { a: 1 } | undefined, k: never) {
    t[null!];
    t["a"];
    x["a"];
    x[k];
}
