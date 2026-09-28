// @strict: true
// @noEmit: true
// tsc: clean. A spread of a generic array-like (constraint assignable to `readonly any[]`, a conditional type here)
// stays itself, a variadic argument (getSpreadArgumentType, isArrayLikeType): tapable's AsArray<T> in webpack
type AsArray<T> = T extends any[] ? T : [T];
function f<T>(getArgs: () => AsArray<T>, fn: (...args: AsArray<T>) => void) {
    fn(...getArgs());
}
