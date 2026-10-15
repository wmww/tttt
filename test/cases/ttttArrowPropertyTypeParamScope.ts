// @declaration: true
// @strict: true
// An arrow function property printed from its expression (the pseudo type) is a scope of its own: the type parameter
// names it gives are free again for its siblings (no b<A_1>)
export const y1 = { a: <A,>(a: A) => {}, b<A>(x: A) {} };
export const y2 = { b<A>(x: A) {}, a: <A,>(a: A) => {} };
export const y3 = { b<A>(x: A) {}, c<A>(x: A) {} };
export const y4 = { a: <A,>(a: A) => {}, c: <A,>(a: A) => {} };
export const y5 = { a: <A,>(a: A) => {}, b<const A>(x: A) {} };
export const y6 = { a: { q: <A,>(a: A) => {} }, b: { r<A>(x: A) {} } };
declare function wrap<T>(f: () => T): { value: T };
export const x = wrap(function () {
  const out = {
    ast: { equals: <A, I>(a: A, b: I) => {} },
    make: { succeed<const A, const B>(input: A, b: B) {} },
  };
  return out;
});
export const z = {
    ast: { equals: <A, I>(a: A, b: I) => {} },
    make: { succeed<const A, const B>(input: A, b: B) {} },
};
