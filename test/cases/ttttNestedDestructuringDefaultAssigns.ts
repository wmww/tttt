// @strict: true
// A default in a pattern that isn't itself the assignment's target is a plain assignment to tsc
// (getAssignedTypeOfBinaryExpression's isDestructuringAssignmentTarget looks one level up): `[[b = 1]] = two` assigns
// b the type of 1 (bun's 21-C16a/b and 99-V4)
declare const one: [string | undefined];
declare const two: [[string | undefined]];
let a: string | number | boolean = true;
let b: string | number | boolean = true;
[a = 1] = one;
[[b = 1]] = two;
const probeA: never = a;
const probeB: never = b;
function f(x: string | number, v: { a: { b: string } }) { ({ a: { b: x = 1 } } = v); x.toFixed(); }
function g(x: string | number, v: { a: string }) { ({ a: x = 1 } = v); x.toFixed(); }
