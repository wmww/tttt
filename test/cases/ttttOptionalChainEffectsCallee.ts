// @strict: true
// A later reference's flow narrowing by an optional call `a?.o()` (tsc's getEffectsSignature) checks the callee not
// to be nullish after taking the chain's marker off: TS18048 at `a.o` besides the call's TS2722 (bun 99-V4
// resolveCallExpression__03). `a?.o?.()`'s callee is the expression of its own `?.`: no error. The flow only gets to
// the call when the reference's type before it isn't the declared one (tsc's branch label stops early), hence `r1`
type T = { m(): number; o?: () => string };
declare const a: T | undefined;
const r1 = a?.m();
const r2 = a?.o();
const r3 = a?.o?.();

declare const c: T | undefined;
const s2 = c?.o();
const s3 = c?.o?.();
