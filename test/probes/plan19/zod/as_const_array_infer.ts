// @strict: true
declare const pick: <T>(xs: readonly T[]) => T;
const lit = pick(["a", 1, true] as const);
const lit2 = pick(["a", "b"] as const);
