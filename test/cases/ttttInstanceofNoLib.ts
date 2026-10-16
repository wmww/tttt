// Without a lib, `instanceof` still narrows: the missing global Object and Function types are the empty object type,
// which any object derives from (tsc's getGlobalType, narrowTypeByInstanceof) (mutation of awaitedTypeNoLib)
// @noLib: true
type NP<T> = T extends Missing ? T : T;
class Th<T> {
  x: T;
  h<R>(a: NP<R> | Th<R>, c: R | Th<R>, d: Th<R> | undefined) {
    if (a instanceof Th) {
      const a2: Th<R> = a;
    }
    if (c instanceof Th) {
      const c2: Th<R> = c;
    } else {
      c;
    }
    if (!(d instanceof Th)) {
      d;
    }
  }
}
