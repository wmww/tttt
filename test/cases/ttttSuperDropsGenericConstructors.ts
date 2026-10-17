// `super(...)` takes the base's constructors that take as many type arguments as the base clause gives (tsc's
// getConstructorsForTypeArguments): a clause without them drops a generic constructor without defaults, so the
// arity error is about the constructor left; none left is a call checked against nothing
interface Both {
    new <T>(x: T, y: string): { a: T };
    new (x: number): { a: number };
}
declare const Ctor: Both;
class E extends Ctor {
    constructor() {
        super(1, 2);
    }
}
interface OnlyGeneric {
    new <T>(x: T, y: string): { a: T };
}
declare const Generic: OnlyGeneric;
class D extends Generic {
    constructor() {
        super(1, 2);
    }
}
interface Counts {
    new <T>(x: T): { a: T };
    new <T, U>(x: T, y: U): { a: T };
}
declare const Counted: Counts;
class F extends Counted<number, string> {
    constructor() {
        super(1);
    }
}
