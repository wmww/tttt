// @strict: true
// A tuple derives from Array (its base type is the array of its elements, a readonly one's from ReadonlyArray, which
// `instanceof Array` counts too), and from a tuple of its shape (bun's 20-C6, 19-C6a/b)
function d9(x: [number, string] | string) { if (!(x instanceof Array)) { x.toUpperCase(); } }
function d7(x: readonly [number] | string) { if (!(x instanceof Array)) { x.toUpperCase(); } else { x[0]; } }
declare const Ctor: new () => [number, string];
function d6(x: [string, number] | Date) {
    if (x instanceof Ctor) return x[0];
    return x.getTime();
}
