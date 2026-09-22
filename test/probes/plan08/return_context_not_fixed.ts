// @strict: true
declare function map<T, U>(array: readonly T[], f: (x: T, i: number) => U): U[];
declare function map1<T, U>(array: readonly T[], f: (x: T, i: number) => U): U[];
declare const b: readonly string[];
const x: {}[] = map(b, t => t + "");
const y: {}[] = map1(b, t => t + "");
declare function flatMap<T, U extends {}>(array: readonly T[] | undefined, mapfn: (x: T, i: number) => U | readonly U[] | undefined): readonly U[];
const r1 = flatMap(b, s => map1(b, t => s + t));
