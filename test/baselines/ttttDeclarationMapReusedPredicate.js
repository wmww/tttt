//// [test/cases/ttttDeclarationMapReusedPredicate.ts] ////

//// [ttttDeclarationMapReusedPredicate.ts]
// A type predicate the node builder reuses (a class expression's method) maps its parameter name, `asserts` and the
// node's ends like the declaration transformer's (babel-parser's `isValidDirective(stmt): stmt is ...` in a mixin).
interface S { type: string }
interface E extends S { e: 1 }
class P { isValid(stmt: S): boolean { return true; } }
export default (Sup: typeof P) => class X extends Sup {
    isValid(stmt: S): stmt is E { return true; }
    check(x: unknown): asserts x is string {}
    check2(x: unknown): asserts x {}
    g<T>(x: T | S): x is T { return true; }
};


//// [ttttDeclarationMapReusedPredicate.js]
class P {
    isValid(stmt) { return true; }
}
export default (Sup) => class X extends Sup {
    isValid(stmt) { return true; }
    check(x) { }
    check2(x) { }
    g(x) { return true; }
};


//// [ttttDeclarationMapReusedPredicate.d.ts]
interface S {
    type: string;
}
interface E extends S {
    e: 1;
}
declare class P {
    isValid(stmt: S): boolean;
}
export default _default;
declare function _default(Sup: typeof P): {
    new (): {
        isValid(stmt: S): stmt is E;
        check(x: unknown): asserts x is string;
        check2(x: unknown): asserts x;
        g<T>(x: T | S): x is T;
    };
};
//# sourceMappingURL=ttttDeclarationMapReusedPredicate.d.ts.map