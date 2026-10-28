// @strict: true
// A field without a type or an initializer is typed by the constructor's assignments to it (tsc's
// getFlowTypeInConstructor): when they need the field's own type, it is circular, TS7022 at the field like one with an
// initializer (reportCircularityError of the property; bun 99-V4 checkClassLikeDeclaration__06/07,
// getTypeOfVariableOrParameterOrPropertyWorker__02, loop__control_class_constructor)
class C {
  x;
  y;
  constructor() {
    this.x = this.y;
    this.y = this.x;
  }
}
class D {
  x;
  constructor() {
    this.x = this.read();
  }
  read() {
    return this.x;
  }
}
class E {
  a;
  b;
  constructor(o: E) {
    this.a = o.b;
    this.b = o.a;
  }
}
