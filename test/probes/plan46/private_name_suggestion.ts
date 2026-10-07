// @target: es2022
class A {
    #foo = 1;
    #bar() { return 1; }
    static #sfoo = 2;
    m(other: A) {
        this.#fo;
        this.fooo;
        this.#ba();
        other.#fo;
        A.#sfo;
        this.foo;
        this?.#fo;
    }
}
class B extends A {
    n() { this.#fo; this.foo; }
}
const a = new A();
a.foo;
a.#fo;
a["#fo"];
