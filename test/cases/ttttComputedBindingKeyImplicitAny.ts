// @strict: true
// An unannotated parameter pattern's element with a computed key: one usable as a property name (a literal, a unique
// symbol) is in the implied type and implicitly any (TS7031); another one isn't, the element is the implied type indexed
// by the key (tsc's getTypeFromObjectBindingPattern, isTypeUsableAsPropertyName)
export function f1({ ["a"]: a, b }) { return [a, b]; }
export function f2({ [0]: a, ...r }) { return [a, r]; }
export function f3({ [`x`]: a }) { return a; }
export function f5(k: string, { [k]: { x } }) { return x; }
