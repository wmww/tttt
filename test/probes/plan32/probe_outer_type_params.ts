function f<T>(v: T) {
    interface I { x: T }
    const i: I = { x: v };
    return i;
}
const fi = f(3);
const fx = fi.x;
function g<T>(v: T) {
    type A = { y: T };
    const a: A = { y: v };
    return a;
}
const ga = g("s").y;
function h<T>(v: T) {
    return class { z = v; m(): T { return v; } };
}
const H = h(true);
const hz = new H().z;
function k<T>(v: T) {
    class K<U> { z = v; u!: U; }
    return new K<string>();
}
const kk = k(1);
const kz = kk.z;
function Boxed<T>(v: T) {
    class Box { value = v; }
    return new Box();
}
const b = Boxed(3);
const bv = b.value;
