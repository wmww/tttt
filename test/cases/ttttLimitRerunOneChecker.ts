// @noEmit: true
// @filename: lib.ts
// An instantiation limit trips in whichever checker gets there first, and the error type it caches silences the
// rest (tsc's rule): in one checker, lib.ts's indexed access trips first and a.ts's call is silent; a checker that
// starts with a.ts trips at the call. With several checkers (baseline_tests --checkers 2) a limit report reruns the
// program in one checker, so the output is this one at any checker count (main.c's check_files())
interface Box<T> { inner: Unwrap<T> }
export type Unwrap<T> = T extends string ? "s" : T extends number ? "n" : Box<[T]>["inner"];
export declare function f<T>(x: T): Unwrap<T>;
// @filename: a.ts
import { f } from "./lib";
export const x = f(true);
// @filename: b.ts
import { f } from "./lib";
export const y = f(true);
