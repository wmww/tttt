// @strict: true
// A homomorphic mapped type over a type parameter whose constraint is unresolved (the error type, any) has a string
// index signature of the template at string (tsgo's forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType): no
// TS2339/TS7053, `T[string] | undefined`
function f<T extends Unresolved>(d: T) { return (d as Partial<T>).value; }
function h<T extends Unresolved>(d: Partial<T>, k: string) { return d[k]; }

