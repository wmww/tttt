// @strict: true
// tsc's isConstraintPosition goes by the parent node: `(x).a` and `(x).f()` don't read a generic x through its
// narrowed constraint, and a parenthesized predicate argument is no discriminant access (bun's 16-C2, 20-P8)
type AB = { kind: "a", a: 1, f(): void } | { kind: "b", b: 2 };
function f1<T extends AB>(x: T) {
  if (x.kind === "a") {
    x.a;
    (x).a;
    (x)["a"];
    (x).f();
  }
}
type U = { kind: "a"; a: number } | { kind: "b"; b: string };
declare function assertA(k: string): asserts k is "a";
function f2(x: U) { assertA((x.kind)); x.a; }
function f3(x: U) { assertA(x.kind); x.a; }
