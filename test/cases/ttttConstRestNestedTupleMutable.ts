// @strict: true
// @noEmit: true
// An array literal under a const type parameter is readonly unless its apparent contextual type has a mutable
// array-like member: tsc's checkArrayLiteral reads getApparentTypeOfContextualType, so under `...cases: X` with
// `const X extends Item[]` the context `X[1]` of `['b', sa]` is Item, whose `[string, Ad<any>]` keeps the tuple
// mutable and X inferred (not its constraint) without a contextual return type (sphi/osis's enumAdapter tests)
interface Ad<R> { r: R }
type Item = string | [string, Ad<any>];
type ItemRepr<I extends Item> = I extends [infer T extends string, Ad<infer R>] ? [T, R] : I;
declare const sa: Ad<string>;
declare function k<const X extends Item[]>(...cases: X): X;
const e1 = k('a', ['b', sa]);
const z1: number = e1;
declare function h<const X extends Item[]>(...cases: X): { o: { [I in keyof X]: ItemRepr<X[I]> }[number] };
const e2 = h('a', ['b', sa]).o;
const z2: number = e2;
declare function r<const X extends readonly Item[]>(...cases: X): X;
const e3 = r('a', ['b', sa]);
const z3: number = e3;
declare function g<const X extends Item[]>(x: X): X;
const e4 = g(['a', ['b', sa]]);
const z4: number = e4;
