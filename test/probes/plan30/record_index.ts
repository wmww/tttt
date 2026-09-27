// @strict: true
function h<T extends Record<K, number>, K extends string>(array: T[], prop: K): number {
    let result = 0;
    for (const v of array) {
        result += v[prop];
        const x: number = v[prop];
    }
    return result;
}
function g<K extends string>(r: Record<K, number>, k: K) {
    const n: number = r[k];
    r[k] = 1;
}
type M = { a: string; b: number };
function m<K extends keyof M>(o: { [P in keyof M]: M[P][] }, k: K) {
    o[k].push(o[k][0]);
    const v: M[K][] = o[k];
}
function t<T extends { a: string }>(o: T) {
    const s = o["a"];
    const s2: string = s;
}
