// @strict: true
interface SF { sf: 1 }
interface FR { fr: 1 }
declare function concatenate<T>(array1: readonly T[] | undefined, array2: readonly T[] | undefined): readonly T[];
declare function map<T, U>(array: readonly T[], f: (x: T, i: number) => U): U[];
declare let raw: readonly [SF, FR][] | undefined;
declare const sf: SF;
declare const refs: readonly FR[];
raw = concatenate(raw, map(refs, f => [sf, f]));
