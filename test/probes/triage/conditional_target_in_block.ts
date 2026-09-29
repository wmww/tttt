// @strict: true
interface Logic { input: unknown }
function f<T extends Logic, U>(t: T) {
    const a: T extends string ? [number?] : [string?] = [];
    const b: U extends string ? [number?] : [string?] = [];
    const c: T extends string ? number[] : string[] = [];
    const d: T extends string ? 1 : 1 = 1;
    const e: T extends string ? [] : [] = [];
    const g: U extends string ? { a?: 1 } : { a?: 2 } = {};
}
