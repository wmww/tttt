// @strict: true
interface R { name: string }
function f<T>(old: T | undefined, get: (x: T) => R | undefined) {
    const a = old && get(old);
    const b = old && 1;
    const c = old && old;
    return a;
}
function g<T extends string | undefined>(old: T) {
    const a = old && 1;
}
