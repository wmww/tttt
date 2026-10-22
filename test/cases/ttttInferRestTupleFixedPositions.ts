// @strict: true
// @noEmit: true

// A rest parameter of a tuple type starting with fixed elements: those are parameters of their own, and the rest of
// the source goes to what follows them (tsc's applyToParameterTypes with getParameterCount/getEffectiveRestType)
type R0<F> = F extends (...args: [1, ...infer R]) => void ? R : never;
declare const a: R0<(...args: []) => void>;
declare const b: R0<() => void>;
declare const c: R0<(x: 1) => void>;
declare const d: R0<(x: 1, y: 2) => void>;
declare const e: R0<(x: 1, ...ys: string[]) => void>;

type Shift<A extends Array<any>> = ((...args: A) => void) extends (...args: [A[0], ...infer R]) => void ? R : never;
declare const s0: Shift<[]>;
declare const s1: Shift<Shift<[]>>;
declare const s2: Shift<[1, 2, 3]>;

type R1<F> = F extends (a: string, ...args: [number, ...infer R]) => void ? R : never;
declare const f: R1<(a: string) => void>;
declare const g: R1<(a: string, b: number, c: boolean) => void>;
