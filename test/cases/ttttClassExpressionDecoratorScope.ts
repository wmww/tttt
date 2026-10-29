// A class expression's decorators resolve inside it (its type parameters in scope); a class declaration's outside it
// (tsc's resolveNameHelper moves a declaration's decorator to the class's parent); a member's see the class's. bun 01-C3
// @target: es2022
declare function dec(...a: any[]): any;
const c2 = @dec((x: T) => x) class<T> {};
@dec((x: U) => x) class D<U> {}
class E<V> { @dec((x: V) => x) m() {} }
class F<W> { m(@dec((x: W) => x) p: number) {} }
export {};
