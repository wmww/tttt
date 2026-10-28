// @strict: true
// Only an identifier whose parent is the non-null assertion is assumed initialized and, narrowed to null or undefined,
// read as its declared type (tsc's isNonNullExpression(node.Parent) in checkIdentifier and getFlowTypeOfReference):
// `(x)!` is neither (bun 99-V4 getFlowTypeOfReference__06)
let x: string;
const a = x!;
const b = (x)!;
function f(y: string | undefined) {
  if (y === undefined) {
    const p1: never = y!;
    const p2: never = (y)!;
  }
}
