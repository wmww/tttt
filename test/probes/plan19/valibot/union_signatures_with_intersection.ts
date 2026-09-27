// @target: esnext
// @strict: true
// A member of an intersection whose apparent type is a union: its property is the union's, and a union member that
// is an intersection has the signatures of all its parts
function f<T extends readonly [string] | readonly [number, string]>(x: T & readonly [string | number, ...(string | number)[]]) {
  for (const k of x) {}
  const it = x[Symbol.iterator]();
}
declare const g: ((() => number) | (() => string)) & (() => boolean);
const r = g();
