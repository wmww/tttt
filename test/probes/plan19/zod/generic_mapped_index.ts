// @strict: true
type Shape = { [k: string]: number };
function f<T extends Shape>(t: T, key: string) {
    const m: { [k in keyof T]?: true } = {} as any;
    const a = m[key];
    const b = m.foo;
    return [a, b];
}
function g<T extends { a: string; b: number }>(t: T) {
    const m: { [k in keyof T]: boolean } = {} as any;
    const a = m.a;
    const c = m["b"];
    return [a, c];
}
