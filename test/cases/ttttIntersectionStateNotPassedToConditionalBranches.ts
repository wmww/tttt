// @strict: true
// @noEmit: true

// tsc passes IntersectionState on only through a union or intersection's members, constraints, type arguments,
// properties, signatures and index signatures: two conditional types' branches are related without it (a plain
// isRelatedTo). So under IntersectionState.Target a weak branch still gets the common property check (D<X> isn't
// matched, the overload falls back), and under IntersectionState.Source an object literal type branch still has an
// inferable index signature (L<X> & { tag: 1 } is an R<X>)
interface Weak { w?: number }
type C<X> = X extends string ? { a: () => string; z?: 1 } : { b: 1; z?: 1 };
type D<X> = X extends string ? { a: Weak } : { b: 1 };
function f1<X>(c: C<X>) {
    const g: { (x: D<X> & { z?: 1 }): "matched"; (x: unknown): "fallback" } = null!;
    const r = g(c);
}

type L<X> = X extends string ? { x: number } : { y: number };
type R<X> = X extends string ? Record<string, number> : { y: number };
function f2<X>(l: L<X> & { tag: 1 }) {
    const r: R<X> = l;
}
