// A namespace block's export that duplicates another block's (TS2300) is a symbol of its own: a value name in its block
// means it (tsc's local symbol, whose exportSymbol it is), printed qualified; a type name means the first export. A
// namespace `prototype` merged into a class is one too
declare namespace TS {
    class H<T> { a: T; }
}
declare namespace TS {
    class H<T> { b: T; }
    class I<T> extends H<T> { }
    var x: I<number>;
    var xb: typeof x.b;
    var hv: typeof H;
    var h: H<number>;
    var ha: typeof h.a;
}
declare class Foo {}
declare namespace Foo {
    namespace prototype {
        function f(): void;
    }
}
