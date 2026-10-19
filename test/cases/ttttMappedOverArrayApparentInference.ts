// Inference from a generic mapped type over an array-constrained T takes its apparent type, the array (tsc's
// getResolvedApparentTypeOfMappedType): the contextual return type makes the callback's array literal a tuple
// @strict: true
// @target: esnext
// @lib: esnext
// @noEmit: true
declare function mp<U>(f: () => U): U[]
function k<T extends readonly unknown[]>() {
  return mp(() => [1, undefined]) as { [K in keyof T]: [T[K], undefined] }
}
function k2<T extends unknown[]>() {
  return mp(() => [1, undefined]) as { [K in keyof T]: [T[K], undefined] }
}
