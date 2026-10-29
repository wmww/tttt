// @strict: true
// A class expression in `extends` is checked while the base constructor type resolves (tsgo's checkClassExpression):
// relating its `root` to the base's resolves Derived1's base types again, TS2310/TS2506 (known failing, type_core.md)
abstract class Base1 {
    abstract root(): Derived1;
}

class Derived1 extends class extends Base1 {
    root() {
        return undefined;
    }
}
{ }
