// @strict: true
function h<E extends readonly unknown[]>(x: { [P in keyof E]: { bar: E[P] } }) {
  const y = [...x, { bar: "abc" }];
}
