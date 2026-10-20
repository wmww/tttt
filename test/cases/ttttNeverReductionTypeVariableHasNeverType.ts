// @strict: true
// @noEmit: true

// A literal property conflicting with a type parameter's constraint reduces the intersection to never, unless another
// constituent's property is never (tsc's CheckFlags.HasNeverType across all of them): `U`'s `a: never` keeps `p`'s
function f<T extends { a: "y" }, U extends { a: never }>(x: T & U & { a: "x" }, y: T & { a: "x" }, z: T & { a: "x" } & { a?: never }) {
    let p = x;
    let q = y;
    let r = z;
}
