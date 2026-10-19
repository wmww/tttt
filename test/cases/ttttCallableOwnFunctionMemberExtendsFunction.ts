// A callable type with a property of its own that Function has too (lodash's `_.toString(value: any)`) isn't
// assumed to extend Function: tsc's relation compares that property against Function's (chart.js's DeepPartial over
// the DOM reaches `typeof globalThis` and the `_` global through it)
// @strict: true
declare const lo: { <T>(value: T): T; toString(value: any): string };
declare const plain: { <T>(value: T): T; extra: number };
type DeepPartial<T> = T extends Function ? T : T extends object ? { [P in keyof T]?: DeepPartial<T[P]> } : T | undefined;
type R = DeepPartial<typeof lo>;
const r: R = lo;
type F = typeof lo extends Function ? 1 : 0;
const f: F = 0;
type G = typeof plain extends Function ? 1 : 0;
const g: G = 1;
