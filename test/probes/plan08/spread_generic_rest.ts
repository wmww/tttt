export function not<T extends unknown[]>(fn: (...args: T) => boolean): (...args: T) => boolean {
    return (...args) => !fn(...args);
}
export function not2<T extends unknown[]>(fn: (...args: T) => boolean): (...args: T) => boolean {
    return (...args: T) => !fn(...args);
}
