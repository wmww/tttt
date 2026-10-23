// @strict: true
// @target: es2017
// An immediately invoked function assigned to an outer variable returns into the target's type (tsc's
// getContextualReturnType of the IIFE, found up the tree), also when a flow query reaches the assignment before the
// walk does: the closure's return type, inferred first, reads the variable after it (getQuickTypeOfExpression of the
// call asks the function's return type). openclaw's package-update-swap.ts `rollbackResult ??= (async () => ...)()`
class E extends Error {
    readonly reason = "changed";
}
type R = { code: number; reason?: "changed" };
type T = { rollback: () => Promise<R> };
declare const error: unknown;
declare const e2: E;
function f() {
    let rollbackResult: ReturnType<T["rollback"]> | undefined;
    const transaction: T = {
        rollback: () => {
            rollbackResult ??= (async () => {
                return { code: 1, ...(error instanceof E ? { reason: error.reason } : {}) };
            })();
            return rollbackResult;
        },
    };
    return transaction;
}
function g() {
    let r: R | undefined;
    const t = () => {
        r = (() => {
            return { code: 1, reason: e2.reason };
        })();
        return r;
    };
    return t;
}
