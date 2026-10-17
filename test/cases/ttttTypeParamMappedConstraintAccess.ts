// @strict: true
// A type parameter constrained to a generic mapped type has the mapped type's members (resolveMappedTypeMembers over
// the apparent modifiers type): what they lack is TS2339 on the type parameter (check_access let these through)
interface A { a: number }
function f<T, U extends Partial<T>>(u: U) { return u.foo; }
function g<T extends A, U extends Partial<T>>(u: U) { u.foo; return u.a; }
function h<T extends A | { b: string }, U extends Readonly<T>>(u: U) { u.foo; return u.a; }
function i<K extends string, U extends Record<K, number>>(u: U) { return u.foo; }
function j<T extends Record<string, number>, U extends Partial<T>>(u: U) { return u.foo; }
function k<T extends A, K extends keyof T, U extends Pick<T, K>>(u: U) { return u.foo; }
