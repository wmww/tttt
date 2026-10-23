// @noEmit: true

// An array type with a literal length is tuple-like (tsc's isTupleLikeType): an array literal assigned to one isn't
// elaborated into the elements the target has no property for, the error stays on the declaration
type test1 = [...number[]]
type fixed1 = test1 & { length: 2 }
let var1: fixed1 = ["0", 0]

// A real tuple still elaborates into the element
let var2: [number, number] = ["0", 0]
