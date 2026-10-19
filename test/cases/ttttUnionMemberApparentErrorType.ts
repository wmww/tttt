// @strict: true
// @noEmit: true
// A union member whose apparent type is the error type (a type parameter constrained to an unresolved type, or an
// intersection with one) contributes nothing to the union's properties (tsc's createUnionOrIntersectionProperty skips
// it): a union of only such members has none, though each alone is the error type and silent (react-navigation's
// narrowed `T & ({} | null)`)
import type { Unresolved } from "nonexistent";
export function f<E extends Unresolved, B extends Unresolved>(a: E & {}, b: E & null, c: E & { c: 3 }, d: (E & {}) | (E & null),
    x: E | B, y: E | string, z: (E & {}) | { q: 1 }, w: Unresolved | E) {
    a.x;
    b.x;
    c.x;
    d.x;
    x.y;
    y.y;
    z.y;
    w.y;
}
