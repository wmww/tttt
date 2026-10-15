// @declaration: true
// tsgo's printer parenthesizes by precedence: a conditional type in an `extends` clause (a function type's return type
// keeps the clause's context), a type query in an array but not in a union or intersection
export type exactEquals<l, r> =
	(<_>() => _ extends l ? 1 : 2) extends <_>() => _ extends r ? 1 : 2 ? true
	:	false
export type K<T> = T extends Array<T extends 1 ? 2 : 3> ? 1 : 0;
export type U<T> = T extends string | (() => T extends 1 ? 2 : 3) ? 1 : 0;
function cleanup() {}
export const create = (root: number) => { if (root) return cleanup; return null; };
function fv() { return 1; }
export const o = { f: Math.random() ? fv : undefined };
declare function ToastComponent(): void;
export const Toast = Object.assign(ToastComponent, { p: 1 });
export const arr = [cleanup];
