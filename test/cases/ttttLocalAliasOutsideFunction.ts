// A type alias in a top-level block or class static block isn't local (tsc's isLocalTypeAlias: inside a function), so
// it renames an instantiation of an outer alias (and then prints structurally, not being accessible); inside a function
// it keeps the outer alias's name
type Top<T> = { a: T } | { b: T };
{
    type L = Top<string>;
    var x: L = null!;
}
x;
class C {
    static {
        type S = Top<number>;
        var y: S = null!;
        y;
    }
}
function f() {
    type F = Top<boolean>;
    var z: F = null!;
    class D {
        static {
            type G = Top<1>;
            var w: G = null!;
            w;
        }
    }
    return z;
}
