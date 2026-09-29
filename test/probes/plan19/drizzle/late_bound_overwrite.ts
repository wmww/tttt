// @target: es2022
// @strict: true
const K = Symbol();
const NS = { K: K as typeof K };
class A { [K]: string | undefined = ""; ["lit"]: number = 1; }
class B extends A {
    override [NS.K]!: string;
    override ["lit"]!: number;
}
