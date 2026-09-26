// @strict: true
interface Opts<T> { onSuccess?: (x: T) => void; retry?: number }
declare const c: Opts<string> | undefined;
const { onSuccess, ...rest } = c || {};
const v = c || {};
