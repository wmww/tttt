// @strict: true
declare const sym: unique symbol;
type EmptyObject = {[sym]?: never};
type A = {s: string} extends EmptyObject ? true : false;
type B = {s: string} extends {x?: never} ? true : false;
declare const a: A;
declare const b: B;
declare const c: EmptyObject;
const d: EmptyObject = {} as {s: string};
