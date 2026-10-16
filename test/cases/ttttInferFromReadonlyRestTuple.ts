// @strict: true
// Inferring from a lone rest parameter of a tuple type with fixed elements makes a new tuple of its elements, mutable
// unless the inference is to a const type parameter (tsc's applyToParameterTypes: getEffectiveRestType is only the
// tuple's variable tail, getRestTypeAtPosition makes the tuple). arktype's `TypedFn.params`, `Fn<infer params>` of a
// signature taking `...args: readonly [s: string | unknown[]]`, was `Type<readonly [...]>` (fn.test.ts, TS2769)
type Fn<A extends readonly any[] = any[], R = unknown> = (...args: A) => R;
type ParamsOf<F> = F extends Fn<infer P> ? P : never;
declare const fixed: (...args: readonly [s: string, n?: number]) => void;
declare const variadic: (...args: readonly [...string[], number]) => void;
declare const leading: (...args: readonly [a: string, ...rest: number[]]) => void;
declare const array: (...args: readonly string[]) => void;
const a: number = null! as ParamsOf<typeof fixed>;
const b: number = null! as ParamsOf<typeof variadic>;
const c: number = null! as ParamsOf<typeof leading>;
const d: number = null! as ParamsOf<typeof array>;
const e: number = null! as Parameters<typeof fixed>;
declare function constParams<const P extends readonly unknown[]>(f: (...args: P) => void): P;
declare function mutableParams<const P extends unknown[]>(f: (...args: P) => void): P;
const f: number = constParams(fixed);
const g: number = mutableParams(fixed);
