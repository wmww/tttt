// @strict: true
// @noEmit: true
// Every unannotated parameter of a setter with a getter has the getter's type (tsc's getTypeForVariableLikeDeclaration),
// a second, malformed one too: no TS7006 (found by mutating propertyOverridesAccessors2, `set x(value, 1)`)
class Base {
    get x() { return 2; }
    set x(value, 1) { }
}
class B2 {
    set y(value, 1) { }
}
class B3 {
    get z(): string { return ""; }
    set z(a, b) { }
}
