// @strict: true
// A type parameter constrained to `unknown[] | undefined` has no `[Symbol.iterator]` (a union property is every
// member's): spreading it is TS2488 naming T; a for-of's operand is checked as non-null first (TS18048)
declare function takeArray(arr: Array<unknown>): void;
function fn<T extends Array<unknown> | undefined>(arg: T) {
    if (typeof arg !== 42) {
        takeArray(arg);
        for (const p of arg) {  }
        const m = [...arg];
    }
}
function fn2<T extends Array<unknown> | undefined>(arg: T) {
    const m = [...arg];
    for (const p of arg) {  }
}
declare const a: unknown[] | undefined;
const m1 = [...a];
