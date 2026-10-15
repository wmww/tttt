// @strict: true
// A property written twice is TS1117 even when the first value is an assignment (not a destructuring pattern), and
// when the last value is the error type the property prints as its first declaration's type (tsgo's
// serializeTypeForDeclaration of the merged symbol), in the types and in operator errors.
export {}
declare let x: boolean;
declare let out: never[];
const o1 = { a: out = [], a: unresolved1 };
const o2 = { b: 1, b: unresolved2 };
x > { c: out = [], c: unresolved3 };
x > { d: "s", d: unresolved4 };
x > { e: 1, f: () => 1 };
