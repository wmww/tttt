// @strict: true
// getEffectiveConstraintOfIntersection intersects the members' constraints without constraint reduction: the
// intersections of its cross product are its own there, never constrained type variables, so the union keeps them all.
// `string & K`, `number & K` and `symbol & K` made first by the reducing path (the Need<...> below) once made
// `PropertyKey & K & ...` collapse back to the source, a spurious TS2344 (type-fest's object-merge.d.ts)
type Need<T extends PropertyKey> = T;
type Test<First, K extends PropertyKey, J extends PropertyKey> = [
    Need<string & K>, Need<number & K>, Need<symbol & K>, Need<string & J>, Need<number & J>, Need<symbol & J>,
    { [P in keyof First as P extends K ? P extends J ? Need<P> : never : never]: First[P] },
];
