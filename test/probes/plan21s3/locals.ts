function outer() {
    const loc = { a: 1 };
    interface L { l: number }
    function inner(x: typeof loc, y: L): typeof loc { return x; }
    return inner;
}
namespace NS {
    const nloc = 1;
    export function g(x: typeof nloc) {}
}
