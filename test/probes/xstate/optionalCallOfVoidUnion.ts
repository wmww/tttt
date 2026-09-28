// @strict: true
// `f?.()` of a (() => void) | void: the optional call removes void like undefined (tsc's getNonNullableType), no TS2722
// (xstate callback.ts: callbackState.dispose?.())
declare const s: { dispose: (() => void) | void };
s.dispose?.();
declare const f: (() => void) | void;
f?.();
