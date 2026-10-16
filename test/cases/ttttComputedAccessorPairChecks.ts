// @target: es2017
// A getter and setter pair with a computed (literal or late-bound) name agree on abstract (TS2676) and the getter
// is at least as accessible as the setter (TS2808); non-bindable names aren't paired
const k = "y";
declare const s: unique symbol;
declare const d: string;
abstract class A {
    get ["x"](): number { return 1; }
    abstract set ["x"](v: number);
    get [k](): number { return 1; }
    abstract set [k](v: number);
    protected get [s](): number { return 1; }
    set [s](v: number) {}
    private get [d](): number { return 1; }
    set [d](v: number) {}
}
class B {
    get ["x"](): number { return 1; }
    private set ["x"](v: number) {}
    private get [k](): number { return 1; }
    protected set [k](v: number) {}
}
