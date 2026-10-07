class L<RT> extends Object<RT> {
    foo = 1;
}
class X extends L<X> {
    m2() {
        return this.foo;
    }
}
new X().foo;
