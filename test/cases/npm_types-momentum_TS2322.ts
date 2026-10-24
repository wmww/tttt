// origin: npm types-momentum@4.6.12 https://github.com/NGHQ/types-momentum tsconfig.bench.json seed 20261032
// flip: yes
// A spread of an array-like that isn't an array or tuple (a Pick of a tuple without its number index) is its number
// index's element, the error type when there's none: `[...p]` is any[], not what its iterator yields (src/methods/dates.ts:74)
// @strict: true
// @target: esnext
type ArrayLengthMutationKeys = 'splice' | 'push' | 'pop' | 'shift' | 'unshift' | number
type ArrayItems<T extends Array<unknown>> = T extends Array<infer TItems> ? TItems : never
type FixedLengthArray<T extends unknown[]> =
  Pick<T, Exclude<keyof T, ArrayLengthMutationKeys>>
  & { [Symbol.iterator]: () => IterableIterator< ArrayItems<T> > }
type DayStreak = { didWatch: boolean; isoTime: string }
declare const runningStreak: FixedLengthArray<[DayStreak, DayStreak]>;
export const mutable: boolean[] = [...runningStreak];
export const spread = [...runningStreak];
export const pair = [...runningStreak] as const;
