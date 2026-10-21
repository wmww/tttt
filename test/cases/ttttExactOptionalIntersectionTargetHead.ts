// @strict: true
// @exactOptionalPropertyTypes: true
// A target intersection's property that one member declares optional keeps the missing type in its combined type
// even when another member makes it required: tsc's getExactOptionalUnassignableProperties doesn't ask whether it is
// optional, so the head is TS2375 (xstate's spawn options under exactOptionalPropertyTypes, flagflip)
type A = { input?: number } & { [K in "input"]: unknown };
type B = { input?: number } & { [K in "input"]: unknown };
declare let a: A;
export let b: B = a;
interface I1 { input?: number }
interface I2 { input?: number }
declare let c: I1 & { input: unknown };
export let d: I2 & { input: unknown } = c;
type M<K extends string> = { [P in K]: unknown };
declare let e: { input?: number } & M<"input">;
export let f: { input?: number } & { [P in "input"]: unknown } = e;
