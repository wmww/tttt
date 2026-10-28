// @strict: true
// @target: esnext
// Where tsc's getTypeOfAccessors reports an accessor's circularity (bun 99-V4 getTypeOfAccessors__01-03,
// getWriteTypeOfAccessors__06): an auto accessor has no getter, so an unannotated one says nothing (no TS7022) and an
// annotated one reports at its setter, which it doesn't have either (a TS2502 without a file); a getter without an
// annotation whose setter is annotated reports at the setter; a circular accessor's type is any, not the error type, so
// a field of its name declared after it is TS2717
class A {
    accessor a = this.a;
    b = this.b;
}
class B {
    accessor a: B["a"] = 1 as any;
    b: B["b"] = 1 as any;
}
class C {
    get x() { return 1; }
    set x(v: C["x"]) {}
}
class D {
    get x() { return this.x; }
    x: number = 1;
}
