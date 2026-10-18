// @strict: true
// A message's cyclic structure placeholder is `...` (tsc's createElidedInformationPlaceholder without NoTruncation), the
// types baseline's `any`
function g1() {
    function Foo() {}
    Foo.prototype = { constructor: Foo };
    Foo.prototype.a = 1;
}
function g2() {
  function f() { return f }
  const x: number = f;
  type R = R[];
  const o = { a: 1, get self() { return o } };
  const y: number = o;
}
