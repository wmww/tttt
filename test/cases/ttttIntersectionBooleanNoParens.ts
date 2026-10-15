// @declaration: true
// `boolean` and a whole enum in an intersection print as names: no parentheses
enum E { A, B }
declare function f<T>(x: T): T & { tag: 1 };
export const a = f(Math.random() > 0.5);
export const b = f(E.A as E);
