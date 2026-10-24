// @strict: true
// A destructuring whose initializer is a type assertion (or satisfies): the operand takes the written type as context,
// never the outer one, so what the pattern implies is never asked for and its defaults are checked once, with the
// asserted type's properties as context (tsc asks getContextualTypeForInitializerExpression on demand)
export function debug<T, E>({ ok = (v) => v, err = (e) => e } = {} as { ok?(v: T): string; err?(e: E): string }) {}
export function d2({ ok = (v) => v } = {} as { ok?(v: number): number }) {}
export function d3({ ok = (v) => v }: { ok?(v: number): number } = {}) {}
const { ok = (v) => v } = {} as { ok?(v: number): number };
export function d4([a = (v) => v] = [] as [((v: number) => number)?]) {}
type X = { a?: (v: number) => number };
declare const y: unknown;
const { a: a1 = (v) => v } = y as X;
const { a: a2 = (v) => v } = <X>y;
const { a: a3 = (v) => v } = {} satisfies X as X;
