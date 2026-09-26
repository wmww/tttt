// @strict: true
interface Opts { onSuccess?: () => void; retry?: number }
declare const c: Opts | undefined;
declare function red<U>(cb: (acc: U) => U, init: U): U;
const r = red<Record<string, string>>((acc) => acc, {});
const v = c || {};
v.onSuccess;
