// @strict: true
// @noEmit: true
// A class field's own type is asked before its initializer is checked (tsc's checkVariableLikeDeclaration), and an
// instantiated property re-entered while its instantiation is in flight keeps the nested result for the rest of it
// (getTypeOfInstantiatedSymbol caches it): `a = [o.b, o.c]` with `b = o.a` and `c = o.a` is circular once, through b,
// so TS7022 on a and b, not c (bun 99-V4 checkClassLikeDeclaration__01/03/04)
declare const other: C;
class C {
  a = [other.b, other.c];
  b = other.a;
  c = other.a;
}
interface Holder<T> {}
class Base {
  b: unknown = 0;
}
declare const another: D;
class D extends Base implements Holder<D["a"]> {
  a = [another.b, another.c];
  b = another.a;
  c = another.a;
}
