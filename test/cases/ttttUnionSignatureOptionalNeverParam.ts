// A union's combined signature takes the intersection of the parameter types at a position, an optional parameter's
// undefined included: `(number | undefined) & string` is never, not made `undefined` again by the optionality
// (tsc's combineUnionOrIntersectionParameters) (mutation of unionTypeCallSignatures4)
// @strict: true
type F1 = (a: string, b?: string) => void;
type F2 = (a: string, b?: number, c?: string) => void;
type F3 = (a: string, ...rest: string[]) => void;
declare var f: F1 | F2 | F3;
f("a", "b");
declare var g: F2 | F3;
g("a", "b");
g("a");
declare var h: F1 | F2;
h("a", "b");
h("a", undefined);
