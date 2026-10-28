// @strict: true
// @noEmit: true
// A comparison control flow evaluates while a loop's types are partial is reported on those types (TS2367 for the
// first iteration's narrowed operands), unless it is reached from the assigned value only through what passes tsc's
// CheckModeTypeOnly on (getTypeOfExpression of the value: parentheses, a conditional's parts, a binary operator's left
// operand, a comma's right one, an assignment's value, `as`, `void`). bun 99-V4 getTypeOfExpression__01/02,
// getAssignedType__05/06, checks-of-a-value-assigned-on-a-loop-back-edge's a3.ts; controlFlowNoIntermediateErrors is
// the other side
declare const more: boolean;
declare function pick<T>(v: T): "x" | "y";
declare function str(x: unknown): string;
declare const ones: 1[];
declare const zeros: 0[];
function f1() { let a: "x" | "y" = "x"; let b: "x" | "y" = "y"; while (more) { a = pick(a === b); } }
function f2() { let a: 0 | 1 = 0; let x: 0 | 1 | undefined; while (more) { for (x of a === 1 ? ones : zeros) { a = x; } } }
function d01() { let x: string | number = "a"; while (more) { x = more && x === 5 ? "b" : 5; } }
function d02() { let x: string | number = "a"; while (more) { x = x === 5 && more ? "b" : 5; } }
function d06() { let x: string | number = "a"; while (more) { x = (x, x === 5) ? "b" : 5; } }
function d08() { let x: string | number = "a"; while (more) { x = str(x === 5) ? "b" : 5; } }
function d10() { let x: string | number = "a"; while (more) { x = !(x === 5) ? "b" : 5; } }
function d13() { let x: string | number = "a"; while (more) { x = (x === 5 ? 1 : 2) === 1 ? "b" : 5; } }
function d16() { let x: string | number = "a"; while (more) { x = (x === 5) as boolean ? "b" : 5; } }
function d17() { let x: string | number = "a"; while (more) { x = (x === 5) satisfies boolean ? "b" : 5; } }
function d29() { let x: string | number = "a"; let y = false; while (more) { x = (y = x === 5) ? 1 : "a"; } }
function g() { let code: 0 | 1 = 0; while (true) { code = code === 1 ? 0 : 1; } }
