// @strict: true
// @noEmit: true

// An expando of an annotated variable has its object literal value widened (tsc's
// getWidenedTypeForAssignmentDeclaration ends with getWidenedType): no longer fresh, it fails a weak `args` type with
// "no properties in common" on the declaration, next to the assignment's own excess property error
type S = ((a: string) => string) & { args?: Partial<{ ref?: number }> };
export const D: S = (a) => a;
D.args = { disabled: false };
export const E: S = (a) => a;
E.args = { ref: "x" };
