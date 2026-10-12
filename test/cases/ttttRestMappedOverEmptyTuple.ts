// A homomorphic mapped type over T whose constraint includes the empty tuple `[]` is apparently an array (tsc's
// getResolvedApparentTypeOfMappedType): no TS2370/TS2322. lib.es2026.iterator.d.ts's IteratorConstructor.concat is
// this shape, which tttt reported once it checked lib files
// @target: esnext
// @strict: true
declare function concat<T extends readonly unknown[] | []>(...iterables: { -readonly [K in keyof T]: Iterable<T[K]> & object }): T[number];
function f<T extends []>(y: { [K in keyof T]: Iterable<T[K]> }) { const b: readonly any[] = y; }
function g<T extends [] | [string]>(y: { [K in keyof T]: Iterable<T[K]> }) { const b: readonly any[] = y; }
