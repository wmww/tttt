// @strict: true
declare function flatMap<T, U extends {}>(array: readonly T[] | undefined, mapfn: (x: T, i: number) => U | readonly U[] | undefined): readonly U[];
declare function map<T, U>(array: readonly T[], f: (x: T, i: number) => U): U[];
declare function map<T, U>(array: readonly T[] | undefined, f: (x: T, i: number) => U): U[] | undefined;
declare const a: string[] | undefined;
declare const b: readonly string[];
const r1 = flatMap(a, s => map(b, t => s + t));
const r2 = flatMap(a, s => [s]);
const r3 = flatMap(a, s => s);
