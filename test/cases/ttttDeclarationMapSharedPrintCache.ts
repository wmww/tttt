// @declaration: true
// @declarationMap: true
// @strict: true
// tsgo's serializedTypes cache is per enclosing declaration: a class's property declarations (parameter properties
// included) share the class's, a function's parameters share the function's, while a method, an accessor and a return
// type have their own. A type printed again from the same cache is an unmapped clone (xterm's CoreTerminal
// `onRender` after `_onRender`).
declare function mk<T>(): T;
const o = mk<{ s: number, e: number }>();
export function f(a = o, b = o) { return o; }
export class D {
  m(a = o) { return o; }
  constructor(public p = o, q = o) {}
  x = o;
  get g() { return o; }
  y = o;
  [k: string]: any;
}
export class E {
    a = mk<{ s: number, e: number }>();
    b = this.a;
}
