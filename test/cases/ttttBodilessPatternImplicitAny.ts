// @strict: true
// @noEmit: true
// In a bodiless signature a renaming in a parameter pattern is TS2842 alone, but any other element (a shorthand, a
// nested pattern, an array element) asks for the parameter's type, which reports every element of the pattern, the
// renamings too (tsc's checkVariableLikeDeclaration: getTypeForBindingElementParent; mutation testing seed 20261104,
// reverseMappedTypePrimitiveConstraintProperty)
declare function h({ a: b, c }): void;
declare function k({ a: b, d: { e } }): void;
declare function m({ a: b }, { f }): void;
type T = ({ a: b, g }: any) => void;
declare function f2({ a: b }): void;
declare function g2({ a: }): void;
declare function h2({ a: b, "c" }): void;
declare function n([o], { a: p }): void;
