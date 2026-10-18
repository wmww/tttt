// A JSX tag typed as a union with an intersection member (`NoInfer<T> & string`, T a key of IntrinsicElements): tsc's
// getApparentType of the intersection is no union, so it has no signatures and the union of the members' has none
// either (TS2604), not one of every intrinsic element
// @jsx: preserve
// @strict: true
declare namespace JSX {
    interface IntrinsicElements { a: { href?: string }; b: { title?: string } }
    interface Element { }
}
export function C<T extends keyof JSX.IntrinsicElements>(Tag: NoInfer<T> | (NoInfer<T> & string)) {
    return <Tag />;
}
export function D<T extends keyof JSX.IntrinsicElements>(Tag: T) {
    return <Tag />;
}
export function E<T extends keyof JSX.IntrinsicElements>(Tag: T & string) {
    return <Tag />;
}
