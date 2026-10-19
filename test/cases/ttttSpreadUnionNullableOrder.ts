// @strict: true
// A union member null or undefined spread among others makes a new {} with the literal's symbol (tsc's getSpreadType),
// sorted among the other alternatives by creation; an `object` member gives the symbolless empty object, sorted last
type F<S> = (this: S, p: number) => void;
type O<S> = { enter?: F<S>[]; exit?: F<S>[] };
type V<S> = F<S> | O<S>;
interface Vis<S> { a?: V<S>; b?: V<S>; enter?: F<S>; }
function g<S>(v: Vis<S>, k: keyof Vis<S>) {
  const fns = v[k];
  const x: number = { ...fns };
}
function h(fns: (() => void) | { a: number } | undefined) {
  const x: number = { ...fns };
}
function k1(fns: (() => void) | { a: number } | object) {
  const x: number = { ...fns };
}
function k2(fns: { a: number } | object | undefined, g: { c: 1 } | { d: 2 }) {
  const x: number = { ...fns, ...g };
}
