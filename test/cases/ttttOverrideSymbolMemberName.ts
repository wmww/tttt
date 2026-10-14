// @target: es2015
// A member named by a well-known symbol is printed as symbolToString does in checkKindsOfPropertyMemberOverrides'
// messages, `[Symbol.iterator]`, not as its escaped name (`__@iterator@<id>`, which depends on the checker)
declare class Base {
    [Symbol.iterator]: () => Iterator<number>;
    m: () => void;
}
declare class Derived extends Base {
    [Symbol.iterator](): Iterator<number>;
    m(): void;
}
abstract class A {
    abstract [Symbol.toPrimitive](): number;
}
class B extends A {}
