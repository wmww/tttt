// @strict: true
// @noEmit: true
// A never index reads a mapped type's index signature when its keys are string, number or symbol: `Record<string,
// T>[never]` is T (tsc's getPropertyTypeForIndexType), es-toolkit's compat sampleSize. A keyless one is never
type A<T> = Record<string, T>[never];
declare const a: A<number>;
const b: string = a;
type C = { [K in never]: string }[never];
type D = Record<number | "x", boolean>[never];
