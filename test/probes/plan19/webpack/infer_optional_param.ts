// @strict: true
// @noEmit: true
// @target: es2017
// tsc: clean. Inference from an optional parameter sees `string | undefined` on both sides (applyToParameterTypes uses
// getTypeAtPosition): T is `string | undefined` (webpack ConsumeSharedPlugin's `new Promise` with a JSDoc resolve)
const p = new Promise((resolve: (value?: string) => void) => { resolve(); });
declare function h<T>(cb: (r: (v: T) => void) => void): T;
const q = h((r: (v?: string) => void) => {});
