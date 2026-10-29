// @target: es2015
// An intersection with a class reference whose type arguments are generic is reduced like any other (tsc's
// getReducedType reads every member's properties): a mixin's class expression inside a generic function is
// `mixC.(Anonymous class)<typeof A, U>`, whose private `pvt` conflicts with A's (seed 20261113's mixinPrivateAndProtected)
type Constructor<T> = new(...args: any[]) => T;
class A {
    public pb: number = 2;
    private pvt: number = 0;
}
function outer<U>(u: U) {
    function mixC<T extends Constructor<{}>>(Cls: T) {
        return class extends Cls {
            private pvt: number = 0;
            own: U = u;
        };
    }
    const AC = mixC(A);
    const ac = new AC();
    ac.pb;
    class B<V> { private pvt = 1; v!: V; }
    let ab!: A & B<U>;
    ab.pb;
}
