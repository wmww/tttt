// @strict: true
// An assignment to x resets the narrowing of the references it contains through access expressions only (tsc's
// containsMatchingReference): `x.a!.b` and `(x.a).b` keep theirs after `x = y` (bun's 21-C17a/b, 32-C13/b)
function g4(x: { a: { b: string | number } }, y: typeof x) { if (typeof (x.a).b === "string") { x = y; (x.a).b.length; } }
function g1(x: { a?: { b: string | number } }, y: typeof x) { if (typeof x.a!.b === "string") { x = y; x.a!.b.length; } }
function g2(x: { a: string | number } | undefined, y: { a: number }) { if (typeof x!.a === "string") { x = y; x!.a.length; } }
function g3(x: { a: { b: string | number } }, y: typeof x) { if (typeof x.a.b === "string") { x = y; x.a.b.length; } }
