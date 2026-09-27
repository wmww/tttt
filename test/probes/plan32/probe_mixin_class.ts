type Constructor<T> = new (...args: any[]) => T;
class Base { x = 1; }
function Tagged<T extends Constructor<{}>>(superClass: T) {
    class C extends superClass {
        _tag = "";
        constructor(...args: any[]) { super(...args); }
    }
    return C;
}
const TB = Tagged(Base);
const tb = new TB();
tb.x;
tb._tag;
class Sub extends Tagged(Base) { y = this.x; }
