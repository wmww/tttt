// @strict: true
function assign<T extends object>(t: T, arg: T) {
    for (const p in arg) {
        t[p] = arg[p];
    }
}
function copy<T1 extends T2, T2>(first: T1, second: T2): void {
    for (const id in second) {
        (first as any)[id] = second[id];
    }
}
function merge(sourceRanges: (string | undefined)[], destRanges: (string | undefined)[]) {
    for (const key in sourceRanges) {
        destRanges[key] = sourceRanges[key];
    }
}
declare const v: [string] | [string, number];
const v1 = v[1];
declare const w: [string] | [string, number] | [boolean, boolean, boolean];
const w2 = w[2];
