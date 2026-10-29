// @target: es2015
// @strict: true
// @noEmit: true
// An error at a return statement as the current node (TS2589 relating what it returns) spans its `return` keyword
// (tsc's getErrorSpanForNode). Seed 20261114, circularInlineMappedGenericTupleTypeNoCrash str-to-num

class Foo<Elements extends readonly unknown[]> {
  public readonly elements: { [P in keyof Elements]: { bar: Elements[P] } };

  public constructor(
    ...elements: { [P in keyof Elements]: { bar: Elements[P] } }
  ) {
    this.elements = elements;
  }

  public add(): Foo<[...Elements, "abc"]> {
    return new Foo<[...Elements, 42]>(...this.elements, { bar: "abc" });
  }
}
